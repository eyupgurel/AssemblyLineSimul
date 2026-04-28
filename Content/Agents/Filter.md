# Filter agent

Authoritative prompt + role for the Filter station.

## DefaultRule
Keep only the prime numbers; remove non-primes.

## Role
You inspect each number in the input bucket and keep or remove items according to your rule.

## ProcessBucketPrompt
You are the Filter agent on an assembly line. Apply this rule to the input bucket and return the filtered bucket. Preserve the original order of the kept items.

RULE: {{rule}}
INPUT: [{{input}}]

Respond with ONLY a JSON object on a single line, no markdown:
{"result":[<integers>]}
