var gzbz2 = require("./gzbz2");
var util = require("util");
var fs = require("fs");

// Read in our test file
var testfile = process.argv[2] || "test.js";
var enc = process.argv[3];
var data = fs.readFileSync(testfile, enc);
console.log("Got : " + data.length);

// Set output file
var fd = fs.openSync(testfile + ".gz", "w", 0644);
console.log("File opened");

// Create gzip stream
var gzip = new gzbz2.Gzip;
gzip.init({level:3});

// Pump data to be gzbz2
var gzdata = gzip.deflate(data, enc);  // Do this as many times as required
console.log("Compressed chunk size : " + gzdata.length);
fs.writeSync(fd, gzdata, 0, gzdata.length, null);

// Get the last bit
var gzlast = gzip.end();
console.log("Compressed chunk size: " + gzlast.length);
fs.writeSync(fd, gzlast, 0, gzlast.length, null);
fs.closeSync(fd);
console.log("File closed");

// See if we can uncompress it ok
var gunzip = new gzbz2.Gunzip;
gunzip.init({encoding: enc});
var testdata = fs.readFileSync(testfile + ".gz");
console.log("Test opened : " + testdata.length);
var inflated = gunzip.inflate(testdata, enc);
console.log("GZ.inflate.length: " + inflated.length);
gunzip.end(); // no return value

if (data.length != inflated.length) {
    console.log('error! input/output string lengths do not match');
}
