# Security policy

## Reporting a vulnerability

Please report suspected vulnerabilities privately via GitHub's
["Report a vulnerability"](../../security/advisories/new) form rather than a
public issue. You should receive a response within a week.

## Scope notes

- The webhook server intentionally speaks **plain HTTP**: it is designed to
  sit behind a TLS-terminating proxy or tunnel. Never expose it directly to
  the internet, and always configure `secret_token` (requests without the
  matching `X-Telegram-Bot-Api-Secret-Token` header are rejected with 403).
- Bot tokens are secrets. The library never logs them and keeps them out of
  exception messages; keep them out of your code and repository too
  (`.env` is git-ignored for exactly this reason).
