# Container Blocks

This folder lists **file-format level containers** where metadata may live:
segments, chunks, boxes, IFD links, and similar “blocks”.

The goal is to capture **identifiers** (e.g., JPEG APP markers, PNG chunk
types, TIFF tag IDs that point to sub-IFDs or embedded packets) that OpenMeta
can parse/write, independent of any particular implementation.

Data is stored as JSONL in `registry/blocks/blocks.jsonl` with
`kind: "container.block"`.
