OpenMeta
========

OpenMeta is a C++ library for reading metadata safely across common image and
media containers.

What it does today:

- Scans files to locate metadata blocks (EXIF, XMP, ICC, IPTC, MPF, comments).
- Extracts and optionally decompresses block payloads (size-limited).
- Decodes EXIF/TIFF IFD tags into a typed, normalized in-memory model.

OpenMeta treats metadata as **untrusted input** and applies explicit limits and
sanitization to reduce memory and output attack surface.

.. toctree::
   :maxdepth: 2

   build
   development
   testing
   security
   api
