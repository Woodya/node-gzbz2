var gzbz2 = require("./gzbz2");
var util = require("util");
var fs = require("fs");

// Read in our test file
var testfile = process.argv[2] || "test.js";
var enc = process.argv[3];
var data = fs.readFileSync(testfile, enc);
console.log("Got : " + data.length);

["G", "B"].forEach(function(type) {
    console.log("Testing " + type + "zip...");
    var output = testfile + "." + (type == "G" ? "gz" : "bz2");
    
    // Set output file
    var fd = fs.openSync(output, "w", 0644);
    console.log("File opened");
    
    // Create zip stream
    var zip = new gzbz2[type + "zip"];
    zip.init({level:3});
    
    // Pump data to be gzbz2
    var zdata = zip.deflate(data, enc);  // Do this as many times as required
    console.log("Compressed chunk size : " + zdata.length);
    fs.writeSync(fd, zdata, 0, zdata.length, null);
    
    // Get the last bit
    var zlast = zip.end();
    console.log("Compressed chunk size: " + zlast.length);
    fs.writeSync(fd, zlast, 0, zlast.length, null);
    fs.closeSync(fd);
    console.log("File closed");
    
    // See if we can uncompress it ok
    var unzip = new gzbz2[type + "unzip"];
    unzip.init({encoding: enc});
    var testdata = fs.readFileSync(output);
    console.log("Test opened : " + testdata.length);
    var inflated = unzip.inflate(testdata, enc);
    console.log(type + "Z.inflate.length: " + inflated.length);
    unzip.end(); // no return value
    
    if (data.length != inflated.length) {
        console.log('error! input/output string lengths do not match');
    }
});
