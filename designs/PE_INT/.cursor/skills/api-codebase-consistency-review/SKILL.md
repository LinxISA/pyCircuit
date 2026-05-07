---
name: api-codebase-consistency-review
description: Reviews API usage and repository consistency. Use when code calls framework/private APIs, adds helper wrappers, introduces error messages, documentation text, naming conventions, or style-sensitive changes.
---

# API And Codebase Consistency Review

## Instructions

Use this skill during implementation and PR review to catch issues that functional tests rarely expose.

Check whether the change fits the repository's API style, language style, naming conventions, and helper patterns.

## Review Checklist

1. Search for private/protected API usage such as leading-underscore methods.
2. Prefer local public helpers when they exist.
3. If adding one helper in a pair, check whether the symmetric helper is needed.
4. Match repository language for errors, docs, comments, and user-facing messages.
5. Match existing naming style for modules, files, functions, variables, and tests.
6. Avoid introducing one-off local conventions unless the user explicitly asks.
7. Check generated artifacts if API choices affect emitted code.

## Common Failure Patterns

- Calling `_zext` directly while a public `sext` wrapper exists.
- Adding English docs but leaving non-English runtime errors in source.
- Using a module name convention different from the project rule.
- Fixing source naming but not generated artifact naming.
- Adding compatibility wrappers without documenting why they are needed.

## Required Evidence

When closing the task, mention:

- Any private API usage found and removed.
- Any language/style inconsistency fixed.
- Any naming convention verified in source and generated output.
