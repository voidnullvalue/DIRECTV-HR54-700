#!/usr/bin/env python3
"""Play a Jellyfin item on the HR54 through a token-hiding LAN TS proxy."""
import argparse
import http.server
import json
import secrets
import socket
import subprocess
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


def api_json(base, key, path, payload=None):
    body = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(
        urllib.parse.urljoin(base + '/', path.lstrip('/')),
        data=body,
        headers={'X-Emby-Token': key, 'Accept': 'application/json',
                 'Content-Type': 'application/json'},
        method='POST' if payload is not None else 'GET',
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def local_address_for(remote_host):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((remote_host, 8096))
        return probe.getsockname()[0]


def device_profile():
    return {
        'Name': 'DIRECTV HR54 MPEG-TS H264 AC3',
        'MaxStreamingBitrate': 8_000_000,
        'DirectPlayProfiles': [],
        'TranscodingProfiles': [{
            'Container': 'ts', 'Type': 'Video', 'Protocol': 'http',
            'VideoCodec': 'h264', 'AudioCodec': 'ac3',
            'MaxAudioChannels': '2',
        }],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True, help='Jellyfin LAN base URL')
    parser.add_argument('--key-file', required=True, type=Path,
                        help='File containing an API key; never passed to receiver')
    parser.add_argument('--item', help='Jellyfin item ID to play')
    parser.add_argument('--search', help='List matching movie and episode titles')
    parser.add_argument('--limit', type=int, default=20, help='List size (default: 20)')
    parser.add_argument('--bind', help='Host LAN address; inferred by default')
    parser.add_argument('--port', type=int, default=8100)
    parser.add_argument('--receiver', default='192.0.2.10')  # RFC 5737 TEST-NET-1 placeholder
    args = parser.parse_args()

    key = args.key_file.read_text().strip()
    if not key:
        parser.error('API key file is empty')
    base = args.server.rstrip('/')
    server_host = urllib.parse.urlsplit(base).hostname
    bind = args.bind or local_address_for(server_host)

    user = api_json(base, key, '/Users/Me')
    if not args.item:
        query = {'Recursive': 'true', 'IncludeItemTypes': 'Movie,Episode',
                 'Limit': max(1, min(args.limit, 100)),
                 'SortBy': 'SortName', 'SortOrder': 'Ascending'}
        if args.search:
            query['SearchTerm'] = args.search
        listing = api_json(base, key, '/Users/' + user['Id'] + '/Items?' +
                           urllib.parse.urlencode(query))
        print('Matching items:', listing.get('TotalRecordCount', 0))
        for candidate in listing.get('Items', []):
            print(candidate.get('Id'), candidate.get('Type'),
                  candidate.get('Name'))
        return 0

    item = api_json(base, key, '/Items/' + args.item)
    if item.get('MediaType') != 'Video':
        parser.error('Selected item is not video')
    libraries = api_json(base, key, '/Users/' + user['Id'] + '/Items?' +
                         urllib.parse.urlencode({'Recursive': 'true', 'Limit': 1}))
    print('Jellyfin:', item.get('Name'), '| library items:',
          libraries.get('TotalRecordCount'), flush=True)

    playback = api_json(base, key, '/Items/' + args.item + '/PlaybackInfo', {
        'UserId': user['Id'], 'MediaSourceId': args.item,
        'DeviceProfile': device_profile(),
        'EnableDirectPlay': False, 'EnableDirectStream': False,
        'EnableTranscoding': True,
        'AllowVideoStreamCopy': True, 'AllowAudioStreamCopy': False,
        'MaxAudioChannels': 2, 'MaxStreamingBitrate': 8_000_000,
    })
    sources = [source for source in playback.get('MediaSources', [])
               if source.get('TranscodingUrl') and
               source.get('TranscodingContainer') == 'ts']
    if not sources:
        raise RuntimeError('Jellyfin did not offer MPEG-TS transcoding')
    upstream = urllib.parse.urljoin(base + '/', sources[0]['TranscodingUrl'].lstrip('/'))
    # The upstream URL contains a key. Keep it in this process only.
    path = '/play/' + secrets.token_urlsafe(18) + '.ts'

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'

        def log_message(self, _format, *_args):
            pass  # Never log upstream URL, query, or client path.

        def do_GET(self):
            if self.path != path:
                self.send_error(404)
                return
            print('HR54 requested Jellyfin stream', flush=True)
            try:
                request = urllib.request.Request(upstream, headers={'X-Emby-Token': key})
                with urllib.request.urlopen(request, timeout=90) as response:
                    self.send_response(200)
                    self.send_header('Content-Type', 'video/mp2t')
                    self.send_header('Connection', 'close')
                    self.end_headers()
                    while chunk := response.read(64 * 1024):
                        self.wfile.write(chunk)
            except (BrokenPipeError, ConnectionResetError):
                print('HR54 closed stream connection', flush=True)
            except Exception as error:
                print('Stream proxy error:', type(error).__name__, flush=True)

    server = http.server.ThreadingHTTPServer((bind, args.port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    receiver_url = f'http://{bind}:{args.port}{path}'
    print('Proxy ready on', bind, args.port, '(opaque path)', flush=True)
    shell = Path(__file__).resolve().parents[1] / 'tools' / 'hr54.sh'
    result = subprocess.run([str(shell),
                             "/var/opt/hr54/bin/hr54-play-url '" + receiver_url +
                             "'; echo HR54_PLAY_RC=$?"],
                            text=True, capture_output=True, timeout=90,
                            env={**__import__('os').environ, 'HR54_HOST': args.receiver})
    if result.returncode or 'HR54_PLAY_RC=0' not in result.stdout:
        print('Receiver playURL wrapper failed', file=sys.stderr)
        server.shutdown()
        server.server_close()
        return 1
    print('Receiver playURL invoked; serving until interrupted', flush=True)
    try:
        thread.join()
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
