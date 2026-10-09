#!/usr/bin/env python3
"""Compare executed SELinux hook source bodies with pinned upstream (mock kernel services)."""
import argparse
import hashlib
import re
import subprocess
import tempfile
from pathlib import Path

PIN = 'df03912f70d92ff2aa9762ef82d607033d37e1da'
ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def extract(source, name):
    pattern = re.compile(r'^(?:static\s+)?(?:[A-Za-z_][A-Za-z_0-9]*\s+)+\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', re.M)
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise RuntimeError(f'Expected exactly one actual definition of {name}, found {len(matches)}')
    start = matches[0].start()
    pos = matches[0].end() - 1
    depth = 0
    state = 'code'
    while pos < len(source):
        char = source[pos]
        following = source[pos:pos + 2]
        if state == 'line':
            if char == '\n': state = 'code'
        elif state == 'comment':
            if following == '*/': state = 'code'; pos += 1
        elif state in ('string', 'character'):
            if char == '\\': pos += 1
            elif char == ('"' if state == 'string' else "'"): state = 'code'
        elif following == '//': state = 'line'; pos += 1
        elif following == '/*': state = 'comment'; pos += 1
        elif char == '"': state = 'string'
        elif char == "'": state = 'character'
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0: return source[start:pos + 1]
        pos += 1
    raise RuntimeError(f'Unterminated function {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=ROOT / 'cache/KernelSU')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    revision = subprocess.check_output(['git', '-C', str(args.upstream), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PIN: raise RuntimeError(f'Upstream revision mismatch: {revision} != {PIN}')
    upstream_path = args.upstream / 'kernel/feature/selinux_hide.c'
    upstream = upstream_path.read_text()
    committed = subprocess.check_output(['git', '-C', str(args.upstream), 'show', PIN + ':kernel/feature/selinux_hide.c'])
    if upstream.encode() != committed: raise RuntimeError('Pinned upstream source has local modifications')
    current_path = ROOT / 'kernel/selinux_hide.c'
    current = current_path.read_text()
    required = ['my_write_context', 'my_write_access', 'my_setprocattr', 'initialize_fake_status', 'my_sel_open_handle_status', 'ksu_selinux_hide_handle_second_stage', 'ksu_selinux_hide_handle_post_fs_data']
    sources = {}
    for kind, source in [('upstream', upstream), ('backport', current)]:
        pieces = [extract(source, name) for name in required]
        if kind == 'backport':
            # These are production dependencies of initialize_fake_status, not fixture models.
            pieces = [extract(source, name) for name in ['status_capture_allowed', 'normalize_status_snapshot']] + pieces
        sources[kind] = '\n\n'.join(pieces)
    print('Pinned upstream:', PIN)
    print('Current source SHA256:', hashlib.sha256(current.encode()).hexdigest())
    print('Kernel services are controlled stubs; this checks hook branch behavior, not kernel ABI/concurrency.')
    with tempfile.TemporaryDirectory(prefix='ksu-parity-') as temp:
        temp = Path(temp)
        for version in [('5.15', 5, 15), ('6.6', 6, 6), ('6.10', 6, 10), ('6.12', 6, 12)]:
            results = {}
            for kind, bodies in sources.items():
                generated = temp / (kind + '.c')
                generated.write_text((HERE / 'services.h').read_text() + '\n' + bodies + '\n' + (HERE / 'scenarios.c').read_text())
                binary = temp / (kind + '-fixture')
                subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-DTEST_KERNEL_MAJOR=' + str(version[1]), '-DTEST_KERNEL_MINOR=' + str(version[2]), str(generated), '-o', str(binary)], check=True)
                results[kind] = subprocess.check_output([str(binary)], text=True)
            if results['upstream'] != results['backport']:
                import difflib
                print(''.join(difflib.unified_diff(results['upstream'].splitlines(True), results['backport'].splitlines(True), fromfile='pinned upstream', tofile='current backport')))
                raise RuntimeError('Observable parity failed at kernel branch ' + version[0])
            print('PASS', version[0], len(results['upstream'].splitlines()), 'observable scenario results')
            control_results = {}
            for kind, source in [('upstream', upstream), ('backport', current)]:
                names = ['hook_selinux_status_open']
                if kind == 'upstream': names += ['ksu_selinux_hide_unhook']
                names += ['ksu_selinux_hide_enable']
                if kind == 'upstream': names += ['ksu_selinux_hide_disable']
                names += ['selinux_hide_feature_get', 'selinux_hide_feature_set']
                names += ['destroy_backup_policy', 'ksu_selinux_hide_boot_completed'] if kind == 'backport' else ['ksu_selinux_hide_drop_backup_if_unused']
                control = '\n'.join(extract(source, name) for name in names)
                generated = temp / ('control-' + kind + '.c')
                generated.write_text((HERE / 'services.h').read_text() + '\n' + (HERE / 'services-control.h').read_text() + '\n' + sources[kind] + '\n' + control + '\n' + (HERE / 'scenarios.c').read_text().replace('int main(void)', 'int hooks_only_main(void)') + '\n' + (HERE / 'scenarios-control.c').read_text() + '\nint main(void) {control_cases();return 0;}\n')
                binary = temp / ('control-' + kind)
                subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-DTEST_KERNEL_MAJOR=' + str(version[1]), '-DTEST_KERNEL_MINOR=' + str(version[2]), '-DTEST_BACKPORT=' + ('1' if kind == 'backport' else '0'), str(generated), '-o', str(binary)], check=True)
                control_results[kind] = subprocess.check_output([str(binary)], text=True)
            if control_results['upstream'] != control_results['backport']:
                import difflib
                print(''.join(difflib.unified_diff(control_results['upstream'].splitlines(True), control_results['backport'].splitlines(True), fromfile='pinned upstream lifecycle', tofile='current lifecycle')))
                raise RuntimeError('Observable lifecycle parity failed at ' + version[0])
            print('PASS', version[0], len(control_results['upstream'].splitlines()), 'stateful lifecycle results; intentional requested-vs-actual getter difference verified')

            if version[0] == '5.15':
                # Verify the fixture can detect the concrete omissions found in the
                # earlier backport, rather than merely accepting two equal copies.
                for label, before, after in [
                    ('access seqno', 'avd.seqno = 1;', 'avd.seqno = 37;'),
                    ('late status normalization', 'status->sequence = 0;', 'status->sequence = 29;'),
                    ('preserved nonzero enforcement', 'if (!status->enforcing)', 'if (true)'),
                    ('newline forwarding', 'str[size - 1] = 0;', '/* deliberately retain newline */'),
                ]:
                    body = sources['backport']
                    if body.count(before) != 1:
                        raise RuntimeError('Mutation anchor changed: ' + label)
                    mutated = temp / 'mutated.c'
                    mutated.write_text((HERE / 'services.h').read_text() + '\n' + body.replace(before, after) + '\n' + (HERE / 'scenarios.c').read_text())
                    binary = temp / 'mutation-fixture'
                    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-DTEST_KERNEL_MAJOR=5', '-DTEST_KERNEL_MINOR=15', str(mutated), '-o', str(binary)], check=True)
                    output = subprocess.check_output([str(binary)], text=True)
                    if output == results['upstream']:
                        raise RuntimeError('Fixture failed to detect deliberate mutation: ' + label)
                    print('DETECTED deliberate regression:', label)
                for label, before, after, target in [
                    ('backup availability after unused boot', '!backup_sepolicy || !backup_usable', '!backup_sepolicy', 'control'),
                    ('logical status disarming', '&& READ_ONCE(status_armed)', '', 'hooks'),
                ]:
                    body = sources['backport']
                    names = ['hook_selinux_status_open', 'ksu_selinux_hide_enable', 'selinux_hide_feature_get', 'selinux_hide_feature_set', 'destroy_backup_policy', 'ksu_selinux_hide_boot_completed']
                    controller = '\n'.join(extract(current, name) for name in names)
                    affected = controller if target == 'control' else body
                    if affected.count(before) != 1: raise RuntimeError('Lifecycle mutation anchor changed: ' + label)
                    if target == 'control': controller = controller.replace(before, after)
                    else: body = body.replace(before, after)
                    mutated = temp / 'mutated-control.c'
                    mutated.write_text((HERE / 'services.h').read_text() + '\n' + (HERE / 'services-control.h').read_text() + '\n' + body + '\n' + controller + '\n' + (HERE / 'scenarios.c').read_text().replace('int main(void)', 'int hooks_only_main(void)') + '\n' + (HERE / 'scenarios-control.c').read_text() + '\nint main(void) {control_cases();return 0;}\n')
                    binary = temp / 'mutation-control-fixture'
                    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-DTEST_KERNEL_MAJOR=5', '-DTEST_KERNEL_MINOR=15', '-DTEST_BACKPORT=1', str(mutated), '-o', str(binary)], check=True)
                    output = subprocess.check_output([str(binary)], text=True)
                    if output == control_results['upstream']: raise RuntimeError('Fixture missed deliberate lifecycle mutation: ' + label)
                    print('DETECTED deliberate regression:', label)




if __name__ == '__main__': main()
