{
  'targets': [
    {
      'include_dirs': ['/usr/include', '/usr/local/include'],
      'libraries': ['-L/usr/lib', '-L/usr/local/lib'],
      'target_name': 'gzbz2',
      'sources': ['compress.cc'],
      'link_settings': {
        'libraries': [
          '-lbz2'
        ]
      },
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
        ],
        ['OS=="mac"',
          {
            'include_dirs': ['/opt/local/include'],
            'libraries': ['-L/opt/local/lib'],
            'cflags': [ '-Wall', '-O2', '-fexceptions' ],
            'cflags_cc!': [ '-fno-exceptions' ],
            'xcode_settings': {
                'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
                'GCC_ENABLE_CPP_RTTI': 'YES'
            },
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
