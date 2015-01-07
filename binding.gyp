{
  'targets': [
    {
      'include_dirs': ['/usr/include', '/usr/local/include'],
      'libraries': ['-L/usr/lib', '-L/usr/local/lib'],
      'target_name': 'gzbz2',
      'sources': ['compress.cc'],
      'conditions': [
        ['OS=="linux"',
          {
            'cflags': [ '-Wall', '-O2', '-fexceptions' ],
            'cflags_cc!': [ '-fno-exceptions' ],
            'defines': [
              '_FILE_OFFSET_BITS=64',
              '_LARGEFILE_SOURCE',
              'WITH_GZIP',
              'WITH_BZIP'
            ],
            'configurations': {
              'Debug': {
                'cflags': ['-O0', '-g3'],
                'cflags!': ['-O2']
              }
            }
          }
        ]
      ]
    },
    {
      'target_name': 'copy_binary',
      'type': 'none',
      'dependencies': [ 'gzbz2' ],
      'copies': [
        {
          'files': [ '<(PRODUCT_DIR)/gzbz2.node' ],
          'destination': '<(module_root_dir)'
        }
      ],

    }
  ]
}
