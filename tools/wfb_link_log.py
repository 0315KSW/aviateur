#!/usr/bin/env python3
"""
WFB-ng 수신 링크 로거 (AGX/라즈베리파이 수신 게이트웨이용)

wifibroadcast@gs 서비스의 JSON API(기본 127.0.0.1:8103)에 붙어서,
1초마다 오는 수신 통계를 CSV로 저장합니다. 표준 라이브러리만 씁니다.

기록 항목 (스트림별, 1초 단위)
  recv_pkts / recv_mbps : 안테나로 받은 전체 패킷 수와 속도 (무선 구간)
  out_pkts  / out_mbps  : 복호·FEC 복구 후 PC 쪽으로 내보낸 패킷 수와 속도
  lost                  : FEC로도 못 살린 패킷 수  (0이 아니면 화면이 깨짐)
  fec_rec               : FEC로 살려낸 패킷 수
  bad / dec_err         : 깨진 패킷 / 복호 실패 패킷
  fec_k / fec_n         : 현재 세션의 FEC 설정
  freq / mcs / bw       : 받은 패킷의 주파수(MHz)·MCS·대역폭
  antN_rssi / antN_snr  : 안테나별 평균 RSSI(dBm)·SNR(dB)

실행:  python3 wfb_link_log.py
종료:  Ctrl+C
결과:  ~/wfb_logs/wfb_YYYYmmdd_HHMMSS.csv  (+ 원본 .jsonl)

송신 쪽(drone 프로필, 미니PC)에서는:  python3 wfb_link_log.py --port 8102
  → 송신 통계가 wfb_..._tx.csv 에 따로 저장됩니다.
  incoming : 카메라에서 들어온 패킷/속도
  injected : 실제로 무선에 쏜 패킷/속도 (FEC 포함)
  dropped  : 송신 단계에서 못 쏘고 버린 패킷 (0이 아니면 송신 쪽 병목)
  fec_timeouts, truncated, lat_avg(us, 주입 지연)
"""

import argparse
import csv
import datetime
import json
import os
import socket
import sys
import time

ANT_SLOTS = 4  # CSV에 남길 안테나 칸 수 (RTL8812AU 1개 = 2칸)


def pair(pkts, key):
    """packets 항목은 [이번 구간 값, 누적 값] 쌍. 이번 구간 값만 꺼낸다."""
    v = pkts.get(key)
    if isinstance(v, (list, tuple)) and v:
        return int(v[0])
    return 0


def open_outputs(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    csv_path = os.path.join(out_dir, 'wfb_%s.csv' % stamp)
    raw_path = os.path.join(out_dir, 'wfb_%s.jsonl' % stamp)
    cols = ['time', 'stream', 'interval_s',
            'recv_pkts', 'recv_mbps', 'out_pkts', 'out_mbps',
            'lost', 'fec_rec', 'bad', 'dec_err',
            'fec_k', 'fec_n', 'freq', 'mcs', 'bw']
    for i in range(ANT_SLOTS):
        cols += ['ant%d_id' % i, 'ant%d_pkts' % i, 'ant%d_rssi' % i, 'ant%d_snr' % i]
    f_csv = open(csv_path, 'w', newline='', encoding='utf-8')
    w = csv.DictWriter(f_csv, fieldnames=cols)
    w.writeheader()
    f_raw = open(raw_path, 'w', encoding='utf-8')
    tx_cols = ['time', 'stream', 'interval_s', 'incoming_pkts', 'incoming_mbps',
               'injected_pkts', 'injected_mbps', 'dropped', 'truncated', 'fec_timeouts',
               'lat_avg_us', 'lat_max_us']
    tx_path = os.path.join(out_dir, 'wfb_%s_tx.csv' % stamp)
    f_tx = open(tx_path, 'w', newline='', encoding='utf-8')
    w_tx = csv.DictWriter(f_tx, fieldnames=tx_cols)
    w_tx.writeheader()
    return csv_path, f_csv, w, f_raw, f_tx, w_tx


def row_from_rx(msg, last_ts):
    pkts = msg.get('packets', {})
    ts = float(msg.get('timestamp', time.time()))
    sid = msg.get('id', '?')
    interval = ts - last_ts[sid] if sid in last_ts else 1.0
    if interval <= 0.05 or interval > 10:
        interval = 1.0
    last_ts[sid] = ts

    session = msg.get('session') or {}
    ants = sorted(msg.get('rx_ant_stats') or [], key=lambda a: a.get('ant', 0))

    row = {
        'time': datetime.datetime.fromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3],
        'stream': sid,
        'interval_s': round(interval, 3),
        'recv_pkts': pair(pkts, 'all'),
        'recv_mbps': round(pair(pkts, 'all_bytes') * 8 / 1e6 / interval, 3),
        'out_pkts': pair(pkts, 'out'),
        'out_mbps': round(pair(pkts, 'out_bytes') * 8 / 1e6 / interval, 3),
        'lost': pair(pkts, 'lost'),
        'fec_rec': pair(pkts, 'fec_rec'),
        'bad': pair(pkts, 'bad'),
        'dec_err': pair(pkts, 'dec_err'),
        'fec_k': session.get('fec_k', ''),
        'fec_n': session.get('fec_n', ''),
        'freq': ants[0].get('freq', '') if ants else '',
        'mcs': ants[0].get('mcs', '') if ants else '',
        'bw': ants[0].get('bw', '') if ants else '',
    }
    for i in range(ANT_SLOTS):
        a = ants[i] if i < len(ants) else {}
        row['ant%d_id' % i] = a.get('ant', '')
        row['ant%d_pkts' % i] = a.get('pkt_recv', '')
        row['ant%d_rssi' % i] = a.get('rssi_avg', '')
        row['ant%d_snr' % i] = a.get('snr_avg', '')
    return row


def row_from_tx(msg, last_ts):
    pkts = msg.get('packets', {})
    ts = float(msg.get('timestamp', time.time()))
    sid = msg.get('id', '?')
    key = 'tx:' + sid
    interval = ts - last_ts[key] if key in last_ts else 1.0
    if interval <= 0.05 or interval > 10:
        interval = 1.0
    last_ts[key] = ts
    ants = msg.get('tx_ant_stats') or []
    lat_avg = max([a.get('lat_avg', 0) for a in ants] or [''])
    lat_max = max([a.get('lat_max', 0) for a in ants] or [''])
    return {
        'time': datetime.datetime.fromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3],
        'stream': sid,
        'interval_s': round(interval, 3),
        'incoming_pkts': pair(pkts, 'incoming'),
        'incoming_mbps': round(pair(pkts, 'incoming_bytes') * 8 / 1e6 / interval, 3),
        'injected_pkts': pair(pkts, 'injected'),
        'injected_mbps': round(pair(pkts, 'injected_bytes') * 8 / 1e6 / interval, 3),
        'dropped': pair(pkts, 'dropped'),
        'truncated': pair(pkts, 'truncated'),
        'fec_timeouts': pair(pkts, 'fec_timeouts'),
        'lat_avg_us': lat_avg,
        'lat_max_us': lat_max,
    }


def tx_console_line(row):
    return ('%s  %-12s in %4d pkt %6.2f Mbps | injected %4d pkt %6.2f Mbps | dropped %3d | lat %s us' % (
        row['time'][11:19], row['stream'], row['incoming_pkts'], row['incoming_mbps'],
        row['injected_pkts'], row['injected_mbps'], row['dropped'], row['lat_avg_us']))


def console_line(row):
    rssi = [str(row['ant%d_rssi' % i]) for i in range(ANT_SLOTS) if row['ant%d_rssi' % i] != '']
    return ('%s  %-12s recv %4d pkt %6.2f Mbps | out %4d pkt %6.2f Mbps | lost %3d fec_rec %3d bad %d'
            ' | MCS %s BW %s | RSSI %s' % (
                row['time'][11:19], row['stream'], row['recv_pkts'], row['recv_mbps'],
                row['out_pkts'], row['out_mbps'], row['lost'], row['fec_rec'], row['bad'],
                row['mcs'], row['bw'], '/'.join(rssi) or '-'))


def run(host, port, out_dir, show_all):
    csv_path, f_csv, writer, f_raw, f_tx, w_tx = open_outputs(out_dir)
    print('로그 파일: %s' % csv_path)
    print('연결: %s:%d  (종료: Ctrl+C)' % (host, port))
    last_ts = {}
    try:
        while True:
            try:
                sock = socket.create_connection((host, port), timeout=5)
            except OSError as e:
                print('JSON API 연결 실패 (%s). 2초 뒤 다시 시도. 서비스 상태: systemctl status wifibroadcast@gs' % e)
                time.sleep(2)
                continue
            sock.settimeout(10)
            buf = b''
            try:
                while True:
                    try:
                        chunk = sock.recv(65536)
                    except socket.timeout:
                        print('10초 동안 통계가 안 옴. 다시 연결합니다.')
                        break
                    if not chunk:
                        print('연결이 끊김 (서비스 재시작?). 다시 연결합니다.')
                        break
                    buf += chunk
                    while b'\n' in buf:
                        line, buf = buf.split(b'\n', 1)
                        line = line.strip()
                        if not line:
                            continue
                        f_raw.write(line.decode('utf-8', 'replace') + '\n')
                        try:
                            msg = json.loads(line)
                        except ValueError:
                            continue
                        mtype = msg.get('type')
                        if mtype == 'settings':
                            print('연결됨: profile=%s wlans=%s' % (msg.get('profile'), msg.get('wlans')))
                        elif mtype == 'rx':
                            row = row_from_rx(msg, last_ts)
                            writer.writerow(row)
                            if show_all or 'video' in str(row['stream']):
                                print(console_line(row))
                        elif mtype == 'tx':
                            row = row_from_tx(msg, last_ts)
                            w_tx.writerow(row)
                            if show_all or 'video' in str(row['stream']):
                                print(tx_console_line(row))
                    f_csv.flush()
                    f_tx.flush()
                    f_raw.flush()
            finally:
                sock.close()
            time.sleep(1)
    except KeyboardInterrupt:
        print('\n종료. 로그: %s' % csv_path)
    finally:
        f_csv.close()
        f_tx.close()
        f_raw.close()


def main():
    ap = argparse.ArgumentParser(description='WFB-ng 수신 링크 통계를 CSV로 저장')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=8103, help='api_port: 수신 gs=8103, 송신 drone=8102')
    ap.add_argument('--out', default=os.path.expanduser('~/wfb_logs'))
    ap.add_argument('--all', action='store_true', help='video 말고 mavlink/tunnel도 화면에 표시')
    a = ap.parse_args()
    run(a.host, a.port, a.out, a.all)


if __name__ == '__main__':
    sys.exit(main())
