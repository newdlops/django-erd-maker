# E2E Environment Strategy

The end-to-end test suite must run the real VS Code extension against real Django fixture workspaces.

## Requirements
- Use the VS Code extension test host rather than direct function invocation.
- Use a Python interpreter where Django is actually installed.
- Run the real Rust analyzer binary.
- Open real fixture workspaces from `test/fixtures/django`.

## Recommended Environment Layout
- `.e2e-django/` as the default local virtual environment for E2E runs
- Django installed inside that environment
- fixture workspaces configured to use the selected interpreter explicitly during the test run

## Expected Setup Flow
1. Create a dedicated virtual environment for E2E.
2. Install Django into that environment.
3. Point the fixture workspace interpreter selection to that environment during the E2E bootstrap.
4. Run the extension host tests with the real extension command flow.

## Notes
- If Django is missing from the configured interpreter, E2E should fail loudly.
- Mock analyzer output or mock graph payloads are not E2E substitutes.
