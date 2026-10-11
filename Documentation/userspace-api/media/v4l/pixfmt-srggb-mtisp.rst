.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-pix-fmt-mtisp-sbggr8:
.. _v4l2-pix-fmt-mtisp-sgbrg8:
.. _v4l2-pix-fmt-mtisp-sgrbg8:
.. _v4l2-pix-fmt-mtisp-srggb8:
.. _v4l2-pix-fmt-mtisp-sbggr10:
.. _v4l2-pix-fmt-mtisp-sgbrg10:
.. _v4l2-pix-fmt-mtisp-sgrbg10:
.. _v4l2-pix-fmt-mtisp-srggb10:
.. _v4l2-pix-fmt-mtisp-sbggr12:
.. _v4l2-pix-fmt-mtisp-sgbrg12:
.. _v4l2-pix-fmt-mtisp-sgrbg12:
.. _v4l2-pix-fmt-mtisp-srggb12:

*********************************
MediaTek ISP packed Bayer formats
*********************************

Description
===========

These pixel formats are written by the MediaTek camera ISP DMA engines
(for instance the IMGO port of the MT8127 and MT8183 ISP). They are raw
sRGB / Bayer formats with 8, 10 or 12 bits per sample. Each line is stored
as a continuous little-endian bitstream: pixel ``n`` of a line occupies bits
``[n * bpp, (n + 1) * bpp)`` of the line, least significant bit first, with
no padding between pixels. Lines may be padded at the end, as reported by
``bytesperline``.

In other respects these formats are similar to :ref:`V4L2-PIX-FMT-SRGGB8`,
:ref:`V4L2-PIX-FMT-SRGGB10` and :ref:`V4L2-PIX-FMT-SRGGB12`.

.. flat-table::
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Bits per sample
      - Bayer order
    * - ``V4L2_PIX_FMT_MTISP_SBGGR8``
      - 'MBB8'
      - 8
      - BGGR
    * - ``V4L2_PIX_FMT_MTISP_SGBRG8``
      - 'MBG8'
      - 8
      - GBRG
    * - ``V4L2_PIX_FMT_MTISP_SGRBG8``
      - 'MBg8'
      - 8
      - GRBG
    * - ``V4L2_PIX_FMT_MTISP_SRGGB8``
      - 'MBR8'
      - 8
      - RGGB
    * - ``V4L2_PIX_FMT_MTISP_SBGGR10``
      - 'MBBA'
      - 10
      - BGGR
    * - ``V4L2_PIX_FMT_MTISP_SGBRG10``
      - 'MBGA'
      - 10
      - GBRG
    * - ``V4L2_PIX_FMT_MTISP_SGRBG10``
      - 'MBgA'
      - 10
      - GRBG
    * - ``V4L2_PIX_FMT_MTISP_SRGGB10``
      - 'MBRA'
      - 10
      - RGGB
    * - ``V4L2_PIX_FMT_MTISP_SBGGR12``
      - 'MBBC'
      - 12
      - BGGR
    * - ``V4L2_PIX_FMT_MTISP_SGBRG12``
      - 'MBGC'
      - 12
      - GBRG
    * - ``V4L2_PIX_FMT_MTISP_SGRBG12``
      - 'MBgC'
      - 12
      - GRBG
    * - ``V4L2_PIX_FMT_MTISP_SRGGB12``
      - 'MBRC'
      - 12
      - RGGB

The 8-bit formats have the same memory layout as their non-MTISP
counterparts. In the 10-bit formats every four pixels are stored in five
bytes, and in the 12-bit formats every two pixels are stored in three bytes.

Below is the start of a line in V4L2_PIX_FMT_MTISP_SBGGR10 format.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - B\ :sub:`00`\ (bits 7--0)
      - G\ :sub:`01`\ (bits 5--0)

        B\ :sub:`00`\ (bits 9--8)
      - B\ :sub:`02`\ (bits 3--0)

        G\ :sub:`01`\ (bits 9--6)
      - G\ :sub:`03`\ (bits 1--0)

        B\ :sub:`02`\ (bits 9--4)
      - G\ :sub:`03`\ (bits 9--2)

Below is the start of a line in V4L2_PIX_FMT_MTISP_SBGGR12 format.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - B\ :sub:`00`\ (bits 7--0)
      - G\ :sub:`01`\ (bits 3--0)

        B\ :sub:`00`\ (bits 11--8)
      - G\ :sub:`01`\ (bits 11--4)
