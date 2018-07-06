Gerbolyze high-resolution image-to-PCB converter
================================================

Tooling for PCB art is quite limited in both open source and closed source ecosystems. Something as simple as putting a
pretty picture on a PCB can be an extremely tedious task. Depending on the PCB tool used, various arcane incantations
may be necessary and even modestly complex images will slow down most PCB tools to a crawl.

Gerbolyze solves this problem in a toolchain-agnostic way by directly vectorizing bitmap files onto existing gerber
layers. Gerbolyze has been tested against both the leading open-source KiCAD toolchain and the industry-standard Altium
Designer.

Produce high-quality artistic PCBs in three easy steps!
-------------------------------------------------------

Gerbolyze works in three steps.

1. Generate a scale-accurate preview of the finished PCB from your CAD tool's gerber output:
   
   .. code::
        
       $ gerbolyze render top my_gerber_dir preview.png

2. Load the resulting preview image into the gimp or another image editing program. Use it as a guide to position scale your artwork. Create a black-and-white image from your scaled artwork using GIMP's newsprint filter. Make sure most details are larger than about 10px to ensure manufacturing goes smooth.

3. Vectorize the resulting grayscale image drectly into the PCB's gerber files:

   .. code::

        $ gerbolyze vectorize top input_gerber_dir output_gerber_dir black_and_white_artwork.png

