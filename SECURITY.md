# Security Policy

## Supported versions

Security fixes are applied to the current release line. Vaultium 0.3 supports Linux and macOS; Windows is not part of the supported security boundary.

## Reporting a vulnerability

Please do not publish exploitable details in a public issue before a fix is available. Report vulnerabilities privately to the repository owner through GitHub's private vulnerability reporting feature when enabled, or through the maintainer contact channel listed on the repository profile.

Include:

- affected version or commit;
- operating system and architecture;
- reproduction steps;
- expected and observed behavior;
- impact assessment;
- any proof-of-concept material needed to reproduce the issue.

## Security assumptions

Vaultium is a privileged backup tool. A deployment is secure only when the surrounding operating-system permissions are secure as well.

- Backup configuration and credential files must not be writable by untrusted users.
- SSH private keys and database credential files should use owner-only permissions.
- Remote SSH host-key verification should remain strict in production.
- Backup destinations must be trusted filesystems.
- Anyone able to replace the Vaultium executable, its configuration, or its scheduled trigger effectively has the privileges of the account running the backup.
- The experimental agent scaffold is not a production network API and should not be exposed as one.

## Restore boundary

Restore is intentionally treated as a destructive operation. Vaultium verifies an artifact before restore and requires explicit overwrite confirmation before applying destructive database/service changes. Operators should still test restores into isolated destinations before restoring production systems.
