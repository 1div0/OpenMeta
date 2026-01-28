OpenMeta
========

OpenMeta is a C++ library for reading metadata safely across common image and
media containers.

What it does today:

- Scans files to locate metadata blocks (EXIF, XMP, ICC, IPTC, Photoshop IRB, MPF,
  comments).
- Extracts and optionally decompresses block payloads (size-limited).
- Decodes common blocks into a typed, normalized in-memory model:
  EXIF/TIFF-IFD tags, IPTC-IIM datasets, ICC profile header/tag tables, and
  Photoshop IRB (8BIM) resource blocks.

OpenMeta treats metadata as **untrusted input** and applies explicit limits and
sanitization to reduce memory and output attack surface.

.. toctree::
   :maxdepth: 2

   build
   development
   testing
   security
   api
