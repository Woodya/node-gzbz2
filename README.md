INFO
----

gzbz2 - A Node.js interface to streaming gzip/bzip2 compression (built originally from wave.to/node-compress)

supports Buffe or string as both input and output, this is controlled by providing encodings to init (to produce Buffers), or by passing whichever you are using as input (with optional encoding for strings).
Bzip and Gzip have the same interfaces, see Versions for specific options info. Also there are two simple js wrappers for producing usable read streams, gunzipstream.js and bunzipstream.js. 

INSTALL
-------

To install, ensure that you have libz (and libbz2) installed:
* these will be looked for in: /usr/lib, /usr/local/lib, /opt/local/lib (on osx)

npm install gzbz2

or

node-waf configure [--no-bzip] [--no-gzip]

node-waf build

This will put the gzbz2.node binary module in build/default.

Note: on osx, you must run node-waf with python2.6 or greater for configure to work properly.

Quick Gzip example
------------------

    var gzbz2 = require("gzbz2");
    var util = require("util");

    // Create gzip stream
    var gzip = new gzbz2.Gzip;
    // binary string output
    // init also accepts level: [0-9]
    gzip.init({encoding: 'binary'});

    // Pump data to be compressed
    var gzdata1 = gzip.deflate("My data that needs ", "ascii");
    util.puts("Compressed size : "+gzdata1.length);

    // treat string as binary encoded
    var gzdata2 = gzip.deflate("to be compressed. 01234567890.");
    util.puts("Compressed size : "+gzdata2.length);

    var gzdata3 = gzip.end(); // important to capture end data
    util.puts("Last bit : "+gzdata3.length);

    // Normally stream this out as its generated, but just print here
    var gzdata = gzdata1+gzdata2+gzdata3;
    util.puts("Total compressed size : "+gzdata.length);

Quick Gunzip example
--------------------

    var gzbz2 = require("gzbz2");
    var fs = require("fs");

    var gunzip = new gzbz2.Gunzip;
    gunzip.init({encoding: "utf8"});

    var gzdata = fs.readFileSync("somefile.gz", "binary");
    var inflated = gunzip.inflate(gzdata, "binary");
    gunzip.end(); // returns nothing

Quick Gunzip Stream example
---------------------------
    var fs = require('fs'),
        gunzip = require('gzbz2/gunzipstream');
    
    var stream = gunzip.wrap(process.argv[2], {encoding: process.argv[3]});
    stream.on('data', function(data) {
        process.stdout.write(data);
    });
    stream.on('end', function() {
        process.exit(0);
    });

Versions
--------

* 0.1.*:
    * added bzip2 support. same interface. Bzip/Bunzip objects, bzip specific init options
        * bzip.init
            * encoding (the output encoding, undefined/null means Buffers)
            * level [1-9], 1 fastest (least memory), 9 slowest (most memory), default = 1
            * workfactor [0-250], read libbz2 docs, default = 30
        * bunzip.init
            * encoding (the output encoding, undefined/null means Buffers)
            * small (boolean[false]) deflate in slow but small memory mode, default = false
    * buffer capable (on by default, this changes the default api use)
        * buffer input inflate/deflate always allowed, buffer output from same is the default
    * init accepts options object to configure buffer behavior, compression, and inflated encoding etc:
        * Gzip.init({encoding: enc, level: [0-9]}), level controls compression level 0 = none, 1 = lowest etc.
        * Gunzip.init({encoding: enc})
        * if an encoding is provided to init(), then the output of inflate/deflate will be a string
        * when providing encodings (either for input our output) for binary data, 'binary' is the only viable encoding, as base64 is not currenlty supported
    * inflate accepts a buffer or binary string[+encoding[default = 'binary']], output will be a buffer or a string encoded according to init options
    * deflate accepts a buffer or string[+encoding[default = 'utf8']], output will be a buffer or a string encoded according to init options
    * also added two submodules gunzipstream, and bunzipstream
        * use these to quickly/easily decompress a file while writing similar code to regular input streams
* 0.0.*:
    * all string based, encodings in inflate/deflate methods, no init params

Authors
-------
* wave.to: developer of node-compress aka 0.0.1 version and the general framework
* woody.anderson@gmail.com, fixed bugs/memory leaks, added 0.1.* features: buffers, bzip2, npm as gzbz2, 
