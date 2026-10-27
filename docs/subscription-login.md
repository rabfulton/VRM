# Subscription login for chat providers

Status: future consideration; no implementation in this change.
Research snapshot: 27 October 2026. Recheck provider documentation, availability
and subscription eligibility before implementing.

## Purpose

Let users connect an eligible ChatGPT or Grok subscription from Chat Settings,
alongside the existing API-key and local-server options. Preserve the current
character prompt, animation permissions, expression cues and streamed local TTS.

## ChatGPT: preferred first integration

OpenAI documents Sign in with ChatGPT for open-source and locally hosted apps.
Eligible Plus and Pro users can authorize ChatGPT plan usage for supported
Responses API requests. Identity-only sign-in does not authorize inference;
the app must request and verify the plan-usage scopes. This does not grant access
to existing ChatGPT conversations. Paid or remotely hosted products have a
different availability/approval path.

Use the direct OAuth + Responses API integration. A Codex installation or helper
process is not required for our conversation interface.

Proposed work:

1. Add a **Continue with ChatGPT** option to Chat Settings, following provider
   branding and consent guidance.
2. Implement browser authorization with PKCE, state/nonce validation, a loopback
   callback, and ID-token signature/identity validation. Persist the installation's
   host ID and the issued client registration for the selected account/workspace.
3. Store access, refresh and retained identity tokens in the OS keyring. Keep
   secret credentials out of QSettings, transcripts and logs. Replace rotating
   refresh tokens together with their associated access token and expiry.
4. List models available to the signed-in account and add a Responses SSE adapter
   alongside the existing Chat Completions transport.
5. Feed streamed assistant text into the existing character-script and speech
   coordinator. Retain local conversation history and handle completion,
   interruption and failure distinctly.
6. Provide account switching, sign-out, token renewal, expired/revoked-session
   recovery and clear subscription-limit feedback. Switching accounts must cancel
   the active reply and speech session. Keep API-key access selectable; changing
   to separately billed usage should be an explicit user choice.

The documented preview currently requires `store: false`, `stream: true`, and
the needed history in each HTTP request's `input`. Supply the character prompt
through `instructions` or developer messages. It has restrictions on request
fields, hosted tools and input modalities; adapt requests specifically for this
route rather than treating the OAuth token as an interchangeable API key.

This is a moderate feature: a rough initial estimate was a few focused development
days for a working integration, with further time for lifecycle and failure tests.
That estimate is provisional, especially while the provider contract is a preview.
Reuse Qt networking and the existing keyring support; assess a small maintained
OAuth/JWT dependency for token validation rather than implementing cryptography.

Official references:

- [Sign in with ChatGPT quickstart](https://developers.openai.com/siwc/quickstart)
- [Open-source/local app overview](https://developers.openai.com/siwc/token-sharing-open-source)
- [Registration and sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in)
- [Models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference)
- [Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations)

## Grok: investigate an optional official helper

The official Grok CLI supports browser sign-in, headless streaming output and
application integration over ACP (`grok agent stdio`). An optional authenticated
helper process is therefore a candidate integration path, with streamed assistant
messages adapted to our existing chat pipeline.

An equivalent public OAuth contract for direct subscription inference in a generic
desktop chat client was not verified during this investigation. Exact eligible
subscription tiers, allowances and suitability for our companion use case still
need verification with the provider and a live account. Do not present subscription
coverage as confirmed merely because CLI sign-in succeeds.

The helper would add an optional installation/runtime dependency. Investigate its
credential storage against this project's keyring requirement, session lifecycle,
cancellation, process cleanup and how to restrict exposed capabilities to the
avatar actions the user enabled. Its coding-agent defaults may need adapting for
a conversational companion.

Official references:

- [Grok CLI overview and browser authentication](https://docs.x.ai/build/overview)
- [CLI login/reference](https://docs.x.ai/build/cli/reference)
- [Headless streaming and ACP integration](https://docs.x.ai/build/cli/headless-scripting)
- [Subscription and usage FAQ](https://docs.x.ai/grok/faq)

## Suggested sequence and validation

Start with a small chat-provider abstraction around the existing transport, then
implement direct ChatGPT login. Keep the character/speech coordinator independent
of authentication and wire protocol. Evaluate the optional Grok helper afterwards.

Use local OAuth/SSE fixtures for cancelled login, invalid state/identity, missing
scopes, refresh-token rotation, interrupted replies, mid-stream quota failures,
logout and account changes. Confirm credentials stay in the OS keyring. Finish
with a real eligible-account check of model access, streaming, subscription limits,
animation cues and sentence-by-sentence TTS.
