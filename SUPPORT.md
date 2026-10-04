# Support

Use [GitHub Issues](https://github.com/newdlops/django-erd-maker/issues) for bugs
and feature requests. There is no guaranteed support response time.

Include the extension version, VS Code version, extension host OS and CPU,
whether the workspace is local or remote, the command/action used, expected and
actual behavior, and a minimal synthetic reproduction when possible.

Run **Django ERD: Show Logs** to inspect the **Django ERD Maker** output channel.
Logs may contain project paths and model names; review and redact before sharing.
Do not attach production database dumps or private workspace exports.

## Common problems

| Symptom | Check |
| --- | --- |
| Analyzer not found or cannot execute | Install the `darwin-arm64` VSIX on an Apple Silicon macOS 26+ workspace host. Remove development executable overrides when testing a release. |
| Commands unavailable in Restricted Mode | Review the workspace contents, then use VS Code's normal workspace trust controls if appropriate. |
| No project or models found | Open the Django project root. In a multi-root workspace the first folder is used. Inspect discovery diagnostics. |
| A model or relationship is missing | Static analysis may not resolve dynamic declarations, custom fields, or incomplete references. Provide a small source-only reproduction. |
| Layout is slow | Start with the normal layout. Experimental optimization can take longer on large projects. Check the progress notification and logs. |
| Too many connections to follow | Select the model and open Related diagram. Filter by model, field, or direction. |
| Related card disappeared after resizing | Select its relationship again to reveal the card's page. This is a known preview limitation. |

For security reports, follow [SECURITY.md](SECURITY.md) instead of posting
vulnerability details publicly.
