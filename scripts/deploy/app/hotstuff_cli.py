#!/usr/bin/python3

import os
import subprocess

ANSIBLE_METADATA = {
    'metadata_version': '0.1',
    'status': ['preview'],
    'supported_by': 'Ted Yin'
}

DOCUMENTATION = '''
---
module: hotstuff_cli

short_description: Ansible module for hotstuff-client

author:
    - Ted Yin (@Tederminant)
'''

EXAMPLES = '''
'''

RETURN = '''
'''

from ansible.module_utils.basic import AnsibleModule

def run_module():
    # define available arguments/parameters a user can pass to the module
    module_args = dict(
        bin=dict(type='str', required=True),
        cwd=dict(type='str', required=True),
        idx=dict(type='int', required=True),
        cid=dict(type='int', required=True),
        iter=dict(type='int', required=True),
        max_async=dict(type='int', required=True),
        log_dir=dict(type='str', required=True),
    )

    module = AnsibleModule(
        argument_spec=module_args,
        supports_check_mode=False
    )

    expanduser = [
        'bin',
        'cwd',
        'log_dir',
    ]

    for arg in expanduser:
        module.params[arg] = os.path.expanduser(module.params[arg])

    try:
        cmd = [*(module.params['bin'].split())]
        cmd += [
            '--idx', str(module.params['idx']),
            '--cid', str(module.params['cid']),
            '--iter', str(module.params['iter']),
            '--max-async', str(module.params['max_async']),
        ]

        logdir = module.params['log_dir']
        if not (logdir is None):
            stdout = open(os.path.expanduser(
                os.path.join(logdir, 'stdout')), "w")
            stderr = open(os.path.expanduser(
                os.path.join(logdir, 'stderr')), "w")
            nullsrc = open("/dev/null", "r")
        else:
            (stdout, stderr) = None, None

        cwd = module.params['cwd']

        pid = subprocess.Popen(
                cmd,
                cwd=cwd,
                stdin=nullsrc,
                stdout=stdout, stderr=stderr,
                env=os.environ,
                shell=False,
                start_new_session=True).pid
        module.exit_json(
                changed=False,
                status=0, pid=pid, cmd=" ".join(cmd), cwd=cwd)
    except (OSError, subprocess.SubprocessError) as e:
        module.fail_json(msg=str(e), changed=False, status=1)


def main():
    run_module()


if __name__ == '__main__':
    main()
