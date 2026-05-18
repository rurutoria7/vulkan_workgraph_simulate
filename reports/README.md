# Reports

This directory stores report, meeting, presentation, and validation artifacts
for the WorkGraph Vulkan POC. Runtime code and shaders stay in
`examples/workgraph_poc/` and `shaders/glsl/workgraph_poc/`.

## Taxonomy (分類方式)

- `2026-04/monthly/`: April 2026 monthly report material, decks, profiling
  screenshots, and verification exports.
- `2026-04/weekly/`: April 2026 weekly report Markdown and PowerPoint files.
- `2026-04/meeting_260424/`: Notes, action items, roadmap, and decks from the
  2026-04-24 meeting.
- `2026-04/koch_validation/`: Historical Koch snowflake validation images and
  CSV output.
- `PPTX_NAMING.md`: Naming convention for formal PowerPoint decks.

## Monthly Report Files

- Formal deck:
  `2026-04/monthly/decks/260424_MTK_WorkGraph_VulkanPersistentThread_POC_InitialEvaluation.pptx`
- Profiling notes:
  `2026-04/monthly/monthly_report_0424_profile_result.md`
- Profiling screenshots referenced by Markdown:
  `2026-04/monthly/assets/profile/`
- Draft or repair decks:
  `2026-04/monthly/decks/drafts/`
- Deck verification exports:
  `2026-04/monthly/verification/`

## Maintenance Rules

- Keep report assets out of the repository root.
- Put formal `.pptx` files under `decks/`; put workflow-only variants under
  `decks/drafts/`.
- Put screenshots, chart inputs, CSV files, and visual verification outputs next
  to the report that uses them.
- Keep browser profiles, temporary unpacked PPTX directories, and other scratch
  outputs out of `reports/`; they belong in ignored local scratch directories.
