#!/usr/bin/env python3
"""Set only skate.'s launch options and compatibility mapping, with Steam stopped."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

APP = '3354750'
TOKEN = re.compile(r'\s+|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+')

def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

def unquote(value):
    if not value.startswith('"'):
        return value
    return re.sub(r'\\([\\"nrt])', lambda m: {'n':'\n','r':'\r','t':'\t'}.get(m[1], m[1]), value[1:-1])

def parse(text):
    tokens = []
    pos = 0
    for match in TOKEN.finditer(text):
        if match.start() != pos:
            raise ValueError('Unsupported VDF syntax')
        pos = match.end()
        token = match[0]
        if not token.isspace() and not token.startswith('//'):
            tokens.append((token, match.start(), match.end()))
    if pos != len(text):
        raise ValueError('Invalid VDF input')
    i = 0
    def block(nested=False):
        nonlocal i
        result = []
        while i < len(tokens):
            key = tokens[i]; i += 1
            if key[0] == '}':
                if not nested:
                    raise ValueError('Unexpected closing brace')
                return result, key[1]
            if key[0] == '{' or i >= len(tokens):
                raise ValueError('Missing VDF value')
            val = tokens[i]; i += 1
            if val[0] == '{':
                children, close = block(True)
                result.append((unquote(key[0]), children, val[1], close + 1))
            elif val[0] == '}':
                raise ValueError('Missing VDF value')
            else:
                result.append((unquote(key[0]), unquote(val[0]), val[1], val[2]))
        if nested:
            raise ValueError('Unclosed VDF block')
        return result, len(text)
    return block()[0]

def lookup(nodes, key):
    matches = [n for n in nodes if n[0].casefold() == key.casefold()]
    if len(matches) > 1:
        raise ValueError('Duplicate VDF key: ' + key)
    return matches[0] if matches else None

def set_value(text, path, value):
    nodes = parse(text)
    closing = len(text)
    for depth, key in enumerate(path):
        node = lookup(nodes, key)
        if node is None:
            fragment = quote(value)
            for item in reversed(path[depth:]):
                fragment = quote(item) + '\n{\n' + fragment + '\n}' if item != path[-1] else quote(item) + '\t' + fragment
            updated = text[:closing] + '\n' + fragment + '\n' + text[closing:]
            parse(updated)
            return updated
        if depth == len(path) - 1:
            if isinstance(node[1], list):
                raise ValueError('Expected scalar value at ' + key)
            return text[:node[2]] + quote(value) + text[node[3]:]
        if not isinstance(node[1], list):
            raise ValueError('Expected block at ' + key)
        nodes = node[1]
        closing = node[3] - 1
    raise ValueError('Empty VDF path')

def read(path):
    return path.read_bytes().decode('utf-8')

def running():
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid == os.getuid() and (proc/'comm').read_text().strip() in ('steam', 'steamwebhelper'):
                return True
        except (OSError, ProcessLookupError):
            pass
    return False

def atomic(path, data):
    fd, name = tempfile.mkstemp(prefix='.reskate-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        shutil.copymode(path, name)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)

def main():
    home = Path.home()
    candidates = [home/'.steam/steam', home/'.steam/root', home/'.local/share/Steam']
    roots = list(dict.fromkeys(p.resolve() for p in candidates if (p/'config/config.vdf').is_file()))
    if len(roots) != 1:
        raise RuntimeError('Cannot identify a unique native Steam installation.')
    root = roots[0]
    users = lookup(parse(read(root/'config/loginusers.vdf')), 'users')
    if users is None or not isinstance(users[1], list):
        raise RuntimeError('Steam has no recorded users; sign in to Steam first.')
    accounts = []
    for sid, fields, _, _ in users[1]:
        if not sid.isdigit() or not isinstance(fields, list):
            continue
        uid = str(int(sid) & 0xffffffff)
        path = root/'userdata'/uid/'config/localconfig.vdf'
        if path.is_file():
            name = lookup(fields, 'AccountName')
            recent = lookup(fields, 'MostRecent')
            accounts.append((name[1] if name else uid, path, recent is not None and recent[1] == '1'))
    recent = [a for a in accounts if a[2]]
    if len(recent) == 1:
        selected = recent[0]
    elif len(accounts) == 1:
        selected = accounts[0]
    else:
        for i, account in enumerate(accounts, 1):
            print(f'{i}. {account[0]}')
        choice = input('Select the Steam account to configure: ')
        if not choice.isdigit() or not 1 <= int(choice) <= len(accounts):
            raise RuntimeError('No Steam account selected.')
        selected = accounts[int(choice)-1]
    print(f'Configuring skate. for Steam account: {selected[0]}')
    steam = shutil.which('steam')
    if not steam:
        raise RuntimeError('Steam command not found.')
    if running():
        print('Closing Steam normally to apply settings automatically.')
        subprocess.Popen([steam, '-shutdown'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 45
        while running() and time.monotonic() < deadline:
            time.sleep(1)
        if running():
            raise RuntimeError('Steam is still running. Exit it fully and run this setup again.')
    # Read after Steam exits so its final configuration flush is included.
    local, config = selected[1], root/'config/config.vdf'
    options = Path(sys.argv[1]).read_text().strip()
    old = {p: p.read_bytes() for p in (local, config)}
    local_text = set_value(old[local].decode('utf-8'), ['UserLocalConfigStore','Software','Valve','Steam','apps',APP,'LaunchOptions'], options)
    config_text = old[config].decode('utf-8')
    for key, value in [('name','proton_experimental'), ('config',''), ('priority','250')]:
        config_text = set_value(config_text, ['InstallConfigStore','Software','Valve','Steam','CompatToolMapping',APP,key], value)
    changes = {local:local_text.encode(), config:config_text.encode()}
    stamp = time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid())
    for path in changes:
        backup = path.with_name(path.name + '.reskate-backup-' + stamp)
        shutil.copy2(path, backup)
        print('Backup:', backup)
    written = []
    try:
        for path, data in changes.items():
            if running() or path.read_bytes() != old[path]:
                raise RuntimeError('Steam restarted or its config changed; aborting.')
            atomic(path, data)
            written.append(path)
    except Exception:
        for path in reversed(written):
            atomic(path, old[path])
        raise
    print('Applied skate. launch options and Proton Experimental. Restarting Steam.')
    subprocess.Popen([steam, 'steam://nav/games'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print('Steam may download Proton Experimental before Play is available.')

if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('Steam configuration failed:', exc, file=sys.stderr)
        sys.exit(1)
