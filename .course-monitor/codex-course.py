#!/usr/bin/env python3
"""Explicit Codex CLI entry point. Only this invocation is monitored."""
import argparse, datetime, json, os, pathlib, shutil, subprocess, sys, urllib.request, uuid
from policy import project_root, redact, safe_directory, sanitize

def final_events(message):
    """Discard progress, reasoning, response text, command output, and diffs."""
    if message.get('type') != 'item.completed':
        return []
    item = message.get('item', {})
    if item.get('type') == 'command_execution' and isinstance(item.get('command'), str):
        return [dict(type='ai_command_result', command=item['command'], exit_code=item.get('exit_code'), reason=str(item.get('status', 'unknown')))]
    if item.get('type') == 'file_change':
        return [dict(type='ai_file_operation', file=change['path'], operation=str(change.get('kind', 'unknown')), reason=str(item.get('status', 'unknown')))
                for change in item.get('changes', []) if isinstance(change, dict) and isinstance(change.get('path'), str)]
    return []

def main():
    parser = argparse.ArgumentParser(description='Send one stdin prompt to Codex and log final course events. Requires a working Codex CLI.')
    parser.add_argument('--allow-edits', action='store_true', help='Use the Codex workspace-write sandbox; otherwise read-only')
    args = parser.parse_args()
    root = project_root()
    config = json.loads((pathlib.Path(__file__).resolve().parent/'config.json').read_text(encoding='utf8'))
    if config.get('enabled') is not True:
        raise SystemExit('Project recording is disabled')
    binary = shutil.which('codex')
    if not binary:
        raise SystemExit('Codex CLI not found on PATH')
    prompt = sys.stdin.buffer.read(65537)
    if not prompt.strip() or len(prompt) > 65536:
        raise SystemExit('Expected a nonempty UTF-8 prompt, at most 64 KiB, on stdin')
    prompt_text = prompt.decode('utf-8-sig')
    session = str(uuid.uuid4())
    file = safe_directory(root, 'events') / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d') + '.' + session + '.jsonl')
    with file.open('x', encoding='utf8', newline='\n') as journal:
        file.chmod(0o600)
        def emit(event):
            safe = sanitize(dict(event, id=str(uuid.uuid4()), timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(), session=session, source='codex-cli', tool='codex-cli', actor=event.get('actor', 'ai')), root)
            if safe is not None:
                journal.write(json.dumps(safe, ensure_ascii=False, separators=(',', ':'), allow_nan=False)+'\n')
                journal.flush()
        emit(dict(type='session_start', actor='system'))
        emit(dict(type='ai_prompt', prompt=prompt_text))
        code = 1
        end_reason = 'codex_process_exit'
        saw_error = False
        turn_failed = False
        process = None
        seen = set()
        try:
            command = [binary, 'exec', '--json', '--ephemeral', '--sandbox', 'workspace-write' if args.allow_edits else 'read-only', '-C', str(root), '-']
            child_env = os.environ.copy()
            # Respect the user's existing Windows proxy when the CLI has no
            # explicit proxy environment. Do not change system/user settings.
            if os.name == 'nt' and not any(k.lower() in {'http_proxy', 'https_proxy', 'all_proxy'} for k in child_env):
                for scheme, proxy in urllib.request.getproxies().items():
                    if scheme in {'http', 'https', 'no'}:
                        child_env[scheme.upper() + '_PROXY'] = proxy
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None, text=True, encoding='utf8', env=child_env, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            process.stdin.write(prompt_text)
            process.stdin.close()
            for line in process.stdout:
                message = json.loads(line)
                if message.get('type') in {'error', 'turn.failed'}:
                    failure = message.get('error', message.get('message', 'Codex turn failed'))
                    if isinstance(failure, dict):
                        failure = failure.get('message', 'Codex turn failed')
                    saw_error = True
                    if message.get('type') == 'turn.failed':
                        turn_failed = True
                    print('Codex error: ' + redact(str(failure)), file=sys.stderr, flush=True)
                if message.get('type') == 'turn.completed':
                    turn_failed = False
                item = message.get('item', {})
                if message.get('type') == 'item.completed':
                    key = item.get('id')
                    if key and key in seen:
                        continue
                    if key:
                        seen.add(key)
                    for event in final_events(message):
                        emit(event)
                    if item.get('type') == 'agent_message':
                        print(item.get('text', ''), flush=True)
            code = process.wait()
            if turn_failed:
                code = code or 1
            if code != 0:
                end_reason = 'codex_turn_failed' if turn_failed else 'codex_process_failed'
            else:
                end_reason = 'completed_after_retry' if saw_error else 'completed'
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            emit(dict(type='session_end', actor='system', exit_code=code, reason=end_reason))
    return code

if __name__ == '__main__':
    sys.exit(main())
