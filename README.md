# Zero-OS custom shim-logs

In order to provide enduser a way to get their container logs, we expose
to the user some tools to send logs to a remote location.

In a first time, this was implemented in `contd` but if `contd` were restarted for
update or so, logs were not attached anymore. Logs handling was managed by `containerd-shim` which
is made to handle containers and let the daemon restart without loosing connection to container.

Since containerd support [external binary to handle logs](https://gitlab.dev.cncf.ci/containerd/containerd/commit/e6ae9cc64f61fc5f65bdb5a8efeeca23ac1d28ea)
we use this method to fetch logs next-to the container and sending them to user-defined endpoint.

# Implementation

First implementation was made in Go like the example `containerd` provided, but since this
process will be running for each containers which needs logs handling, this can become a lot.

Resource wise, this implementation uses ~160K of memory for each process, which is a lower footprint
than others implementation we tried.

# How it works

It follow the workflow that `containerd` provides:
- Log binary is started with 3 more file descriptors (3, 4 and 5), which are
respectively stdout, stderr and ready notifier.
- As soon as logs are ready, 5 is closed
- 3 and 4 are read async and forwarded to specified endpoint

# Debugging

You can also use this tool to simulate containerd behaviors to write your own shim
module. On the `tools` directory, you have a small tool used to simulate a `shim` process
started like it would be on containerd with samed `fd` attached etc.

# Testing

You can test out-of-box the `shim-logs redis` endpoint by compiling `tools` and creating
small local environment:
```
mkdir -p /var/cache/modules/contd/config/maxux
mkdir -p /var/cache/modules/contd/logs/maxux
chown $USER /var/cache/modules/contd/logs/maxux
```

And set the `/var/cache/modules/contd/config/maxux/debug-logs.json` file:
```json
[
    {
        "type": "redis",
        "data": {
            "stdout": "redis://:foobared@127.0.0.2/stdout",
            "stderr": "redis://127.0.0.1/stderr"
        }
    }
]
```

You can then start `./shim-spawn` on the `tools` directory to forward generated text to redis.
Example below use one connection password protected for stdout.

# Cleanup Testing

Just delete debug directory: `rm -rf /var/cache/modules/contd`
