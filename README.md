This is a fork of the [gzbz2](https://github.com/Woodya/node-gzbz2) NodeJS module (provides gzip and bzip2 (de)compression), updated to work on newer versions of NodeJS (should support 0.10.x to 12.x.x which probably includes 15.x.x) and fixed libbz2 linking.

### Installation

You’ll need development headers/stubs installed for libz (zlib) and libbz2 for the module to compile.

Then use the following command:

```bash
npm install https://github.com/animetosho/node-gzbz2
```

### Examples & Credits

See [original README](https://github.com/Woodya/node-gzbz2/blob/master/README.md)

**Warning**: incorrect usage may result in NodeJS crashing!
I haven’t bothered to fix this issue (grandfathered from the original module), but it usually shouldn’t be a problem.

