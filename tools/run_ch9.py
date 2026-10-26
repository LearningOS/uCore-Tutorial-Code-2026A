#!/usr/bin/env python3
"""Run the supplied chapter 9 acceptance programs in a fresh QEMU instance."""
import argparse
import json
import os
from pathlib import Path
import selectors
import shutil
import socket
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

class Machine:
    def __init__(self, qemu, output, fs_image=None, extra_args=()):
        self.output = output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.disk = self.output / 'disk.img'
        shutil.copyfile(fs_image or ROOT / 'nfs/fs.img', self.disk)
        self.qmp_path = self.output / 'qmp.sock'
        self.qmp_path.unlink(missing_ok=True)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            self.qmp_port = reservation.getsockname()[1]
        args = [qemu, '-machine', 'virt', '-m', '128M', '-smp', '1',
                '-bios', str(ROOT / 'bootloader/rustsbi-qemu.bin'),
                '-kernel', str(ROOT / 'build/kernel'), '-display', 'none',
                '-serial', 'stdio', '-monitor', 'none',
                '-qmp', f'tcp:127.0.0.1:{self.qmp_port},server=on,wait=off',
                '-global', 'virtio-mmio.force-legacy=false',
                '-drive', f'file={self.disk},if=none,format=raw,id=x0',
                '-device', 'virtio-blk-device,drive=x0,bus=virtio-mmio-bus.7',
                '-device', 'virtio-gpu-device,bus=virtio-mmio-bus.6,xres=1280,yres=800',
                '-device', 'virtio-keyboard-device,bus=virtio-mmio-bus.4',
                '-device', 'virtio-mouse-device,bus=virtio-mmio-bus.5']
        args.extend(extra_args)
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, cwd=ROOT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)
        self.serial = bytearray()
        self.position = 0
        self.qmp_socket = None
        self.qmp_file = None

    def pump(self, timeout):
        for key, _ in self.selector.select(timeout):
            data = os.read(key.fd, 65536)
            self.serial.extend(data)
            if not data and self.proc.poll() is not None:
                raise RuntimeError('QEMU exited before acceptance completed')

    def expect(self, text, timeout=15):
        needle = text.encode()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            index = self.serial.find(needle, self.position)
            if index >= 0:
                self.position = index + len(needle)
                return
            self.pump(min(.1, max(0, deadline - time.monotonic())))
        raise TimeoutError(f'timeout waiting for {text!r}\n' + self.serial.decode(errors='replace'))

    def send(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def qmp(self, command, arguments=None):
        if self.qmp_socket is None:
            self.qmp_socket = socket.socket(socket.AF_INET)
            self.qmp_socket.settimeout(10)
            self.qmp_socket.connect(('127.0.0.1', self.qmp_port))
            self.qmp_file = self.qmp_socket.makefile('rwb', buffering=0)
            json.loads(self.qmp_file.readline())
            self.qmp('qmp_capabilities')
        request = {'execute': command}
        if arguments is not None:
            request['arguments'] = arguments
        self.qmp_file.write(json.dumps(request).encode() + b'\n')
        while True:
            reply = json.loads(self.qmp_file.readline())
            if 'error' in reply:
                raise RuntimeError(reply['error'])
            if 'return' in reply:
                return reply['return']

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.serial.extend(self.proc.stdout.read())
        (self.output / 'serial.log').write_bytes(self.serial)
        if self.qmp_file:
            self.qmp_file.close()
        if self.qmp_socket:
            self.qmp_socket.close()
        self.qmp_path.unlink(missing_ok=True)


def read_file(image, name):
    """Read the actual on-disk inode, including the double-indirect region."""
    with open(image, 'rb') as disk:
        def block(n):
            disk.seek(n * 1024)
            return disk.read(1024)
        sb = struct.unpack('<6I', block(1)[:24])
        inode_start = sb[4]
        def inode(n):
            data = block(inode_start + n // 16)[(n % 16) * 64:][:64]
            return struct.unpack('<4HI13I', data)
        def content(ino):
            fields = inode(ino)
            size, links = fields[4], fields[5:]
            out = bytearray()
            for n in range((size + 1023) // 1024):
                if n < 11:
                    address = links[n]
                elif n < 11 + 256:
                    address = struct.unpack('<256I', block(links[11]))[n - 11]
                else:
                    index = n - 11 - 256
                    indirect = struct.unpack('<256I', block(links[12]))[index // 256]
                    address = struct.unpack('<256I', block(indirect))[index % 256]
                out.extend(block(address))
            return bytes(out[:size])
        directory = content(1)
        for off in range(0, len(directory), 32):
            ino, raw_name = struct.unpack('<H30s', directory[off:off + 32])
            if ino and raw_name.split(b'\0')[0].decode() == name:
                return content(ino)
    raise RuntimeError(f'{name} absent from disk')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-riscv64')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/ch9-acceptance')
    args = parser.parse_args()
    machine = Machine(args.qemu, args.output)
    result = {}
    try:
        machine.expect('>> ')
        result['boot_uart_shell'] = 'PASS'
        print('PASS: boot, device initialization and UART shell', flush=True)
        for program, expected in [('filetest_simple', 'file_test passed!'),
                                  ('huge_write', '1MiB written'), ('gui_shape', None)]:
            machine.send(program + '\n')
            if expected:
                machine.expect(expected, timeout=120)
            machine.expect('>> ', timeout=120)
            result[program] = 'PASS'
            print('PASS: ' + program, flush=True)
        screenshot = machine.output / 'gui-shape.ppm'
        machine.qmp('screendump', {'filename': str(screenshot)})
        from check_ch9_image import check
        result['display'] = check(screenshot)
        machine.qmp('stop')
        # Guest DMA writes are complete when the guest's write returns.
        # QEMU's backend flush ensures independent disk inspection sees them.
        machine.qmp('human-monitor-command', {'command-line': 'info block'})
    finally:
        machine.close()
    if read_file(machine.disk, 'filea') != b'Hello, world!':
        raise RuntimeError('filea disk data differs')
    expected = bytes(range(256)) * 4096
    if read_file(machine.disk, 'testf') != expected:
        raise RuntimeError('1 MiB disk data differs')
    result['disk_readback_1MiB'] = 'PASS'
    (machine.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
