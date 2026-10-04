# Security

Security fixes target the latest published preview version. Older previews may
need to be upgraded; no long-term support series is currently promised.

## Reporting a vulnerability

Use the repository's **Security → Report a vulnerability** option if private
reporting is available:
[private reporting](https://github.com/newdlops/django-erd-maker/security/advisories/new).
If that option is unavailable, open an issue asking the maintainer for a private
contact method, without publishing exploit details or sensitive project data.

Include affected versions and platforms, reproduction steps using synthetic
source when possible, expected impact, and any proposed fix. Do not include
credentials, production data, or unrelated private source code.

## Execution model

The extension reads local Python files and executes bundled native analyzer and
layout programs. It declares support only for trusted, filesystem-backed
workspaces. Static parsing does not execute Django project code. The current
package targets Apple Silicon macOS; a remote workspace needs a package for the
remote host, not the desktop client.

Development environment variables can override executable paths and layout input
files. Set them only to files you control. The release package does not contain
development workspace snapshots, caches, training data, or user credentials.

The native executables are platform binaries. Release checks verify executable
permissions, expected architecture, source/archive hashes, and supported system
library dependencies. A prepared package is not a claim of Apple notarization.
