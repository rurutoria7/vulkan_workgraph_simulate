# PPTX Naming Convention

This document defines the repository naming convention for presentation files.

## Repository Location

- Formal report decks live under `reports/YYYY-MM/<report-type>/decks/`.
- Drafts, repair attempts, and workflow-only variants live under `decks/drafts/`.
- Keep generated previews and source screenshots next to the report that uses them.

## Canonical Pattern

```text
YYMMDD_MTK_WorkGraph_<Topic>_<Artifact>_<Purpose>[_<Scope>][_vN].pptx
```

## Field Definitions (欄位說明)

- `YYMMDD`
  - Use the presentation cover date or report date.
  - Do not use the file last-modified date.
- `MTK_WorkGraph`
  - Fixed project prefix for this presentation series.
- `Topic`
  - The main technical topic.
  - Examples: `VulkanPersistentThread`, `ExecuteIndirect`, `Coalescing`, `VisibilityBuffer`
- `Artifact`
  - What is being discussed or evaluated.
  - Examples: `POC`, `Experiment`, `Benchmark`, `Design`, `Architecture`
- `Purpose`
  - Why the deck exists.
  - Examples: `ProjectIntro`, `InitialEvaluation`, `BaselineEvaluation`, `OptimizationPlan`, `OptimizationProgress`, `OptimizationEvaluation`
- `Scope` (optional)
  - Add only when a narrower scope is important.
  - Examples: `RDNA3`, `RX7900XTX`, `Depth6`, `Depth8`, `F1`
- `vN` (optional)
  - Use only when multiple formal variants of the same deck must coexist.
  - Examples: `v2`, `v3`

## Style Rules

- Use ASCII only in the filename.
- Use underscores (`_`) only as field separators.
- Use PascalCase inside each field.
- Do not use spaces, hyphens, or parentheses in formal deck filenames.
- Do not use workflow noise in formal deck filenames.
  - Avoid: `FINAL`, `final2`, `fixed`, `baseStyle`, `draft-copy`, `(1)`
- Keep the filename focused on content and intent, not edit history.

## Current Series Examples

- `260424_MTK_WorkGraph_VulkanPersistentThread_POC_InitialEvaluation.pptx`
- `2605xx_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationPlan.pptx`
- `2605xx_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationProgress.pptx`
- `2605xx_MTK_WorkGraph_VulkanPersistentThread_POC_OptimizationEvaluation.pptx`

## Naming Guidance

- Prefer stable topic names across a related series of decks.
- Change `Purpose` before changing `Topic` when the subject is the same but the report phase changes.
- Add `Scope` only when it helps distinguish a meaningful technical slice.
- Use `BaselineEvaluation` when the deck establishes a comparison point for later optimization work.
- Use `InitialEvaluation` when the deck is the first formal readout of a new POC or experiment.

## Quick Checklist

Before saving a new deck, verify:

- The date matches the report cover date.
- The topic is the primary technical subject.
- The artifact describes what is being built or studied.
- The purpose describes why this deck exists.
- No workflow-only words are left in the filename.
