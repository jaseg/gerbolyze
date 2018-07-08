Gerbolyze high-resolution image-to-PCB converter
================================================

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/sample.jpg

Tooling for PCB art is quite limited in both open source and closed source ecosystems. Something as simple as putting a
pretty picture on a PCB can be an extremely tedious task. Depending on the PCB tool used, various arcane incantations
may be necessary and even modestly complex images will slow down most PCB tools to a crawl.

Gerbolyze solves this problem in a toolchain-agnostic way by directly vectorizing bitmap files onto existing gerber
layers. Gerbolyze has been tested against both the leading open-source KiCAD toolchain and the industry-standard Altium
Designer. Gerbolyze is written with performance in mind and will happily vectorize tens of thousands of primitives,
generating tens of megabytes of gerber code without crapping itself. With gerbolyze you can finally be confident that
your PCB fab's toolchain will fall over before yours does if you overdo it with the high-poly anime silkscreen.

Produce high-quality artistic PCBs in three easy steps!
-------------------------------------------------------

Gerbolyze works in three steps.

1. Generate a scale-accurate preview of the finished PCB from your CAD tool's gerber output:
   
   .. code::
        
       $ gerbolyze render top my_gerber_dir preview.png

2. Load the resulting preview image into the GIMP or another image editing program. Use it as a guide to position scale your artwork. Create a black-and-white image from your scaled artwork using GIMP's newsprint filter. Make sure most details are larger than about 10px to ensure manufacturing goes smooth.

3. Vectorize the resulting grayscale image drectly into the PCB's gerber files:

   .. code::

        $ gerbolyze vectorize top input_gerber_dir output_gerber_dir black_and_white_artwork.png

Image preprocessing
-------------------

Nice black-and-white images can be generated from any grayscale image using the GIMP's newsprint filter. The straight-forward pre-processing steps necessary for use by ``gerbolyze vectorize`` are as follows.

1 Import a render of the board generated using ``gerbolyze render``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``gerbolyze render`` will automatically scale the render such that ten pixels in the render correspond to 6mil on the board, which is about the smallest detail most manufacturers can resolve on the silkscreen layer. You can control this setting using the ``--fab-resolution`` and ``--oversampling`` options. Refer to ``gerbolyze --help`` for details.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/01import01.png

2 Import your desired artwork
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Though anime or manga pictures are highly recommended, you can use any image including photographs. Be careful to select a picture with comparatively low detail that remains recognizable at very low resolution. While working on a screen this is hard to vizualize, but the grain resulting from the low resolution of a PCB's silkscreen is quite coarse.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/02import02.png

3 Paste the artwork onto the render as a new layer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/03paste.png

4 Scale, rotate and position the artwork to the desired size
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/04scale_cut.png

For alignment it may help to set the artwork layer's mode in the layers dialog to ``overlay``, which makes the PCB render layer below shine through more. If you can't set the layer's mode, make sure you have actually made a new layer from the floating selection you get when pasting one image into another in the GIMP.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/05position.png

5 Convert the image to grayscale
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/06grayscale.png

6 Fine-tune the image's contrast
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To look well on the PCB, contrast is critical. If your source image is in color, you may have lost some contrast during grayscale conversion. Now is the time to retouch that using the GIMP's color curve tool.

When using the GIMP's newsprint filter, bright grays close to white and dark grays close to black will cause very small dots that might be beyond your PCB manufacturer's maximum resolution. To control this case, add two steps to the grayscale value curve as in the picture below. These steps saturate very bright grays to white and very dark grays to black while preserving the values in the middle.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/08curve_cut.png

7 Retouch details
~~~~~~~~~~~~~~~~~

Therer might be small details that don't look right yet, such as the image's background color or small highlights that merge into the background now. You can manually change the color of any detail now using the GIMP's flood-fill tool.

If you don't want the image's background to show up on the final PCB at all, just make it black.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/09retouch.png

In the following example, I retouched the highlights in the hair of the character in the picture to make them completely white instead of light-gray, so they still stand out nicely in the finished picture.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/10retouched.png

8 Run the newsprint filter
~~~~~~~~~~~~~~~~~~~~~~~~~~

Now, run the GIMP's newsprint filter, under filters, distorts, newsprint.

The first important settings is the spot size, which should be larger than your PCB's minimum detail size (about 10px with ``gerbolyze render`` default settings).

The second important setting is oversampling, which should be set to four or slightly higher. This improves the result of the edge reconstruction of ``gerbolyze vectorize``.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/11newsprint.png

The following are examples on the detail resulting from the newsprint filter.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/12newsprint.png

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/13newsprint.png

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/14newsprint.png

9 Export the image for use with ``gerbolyze vectorize``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Simply export the image as a PNG file. Below are some pictures of the output ``gerbolyze vectorize`` produced for this example.

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/14result_cut.png

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/15result_cut.png

.. image:: https://raw.githubusercontent.com/jaseg/gerbolyze/master/screenshots/16result_cut.png

