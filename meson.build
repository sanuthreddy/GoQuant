project('bitfinex_gateway', 'cpp',
  version : '1.0.0',
  default_options : ['cpp_std=c++17', 'buildtype=debugoptimized']
)
# Test-specific macro
is_test = get_option('is_test')
if is_test
  add_project_arguments('-DTEST_BUILD', language : 'cpp')
endif

# Dependencies
singular_dep = dependency('singular', method: 'pkg-config', required: true)
gateway_dep = dependency('gateway', method: 'pkg-config', required: true)
gtest_dep = dependency('gtest', method: 'pkg-config', required: true)
gmock_dep = dependency('gmock', method: 'pkg-config', required: true)
gtest_main_dep = dependency('gtest_main', method: 'pkg-config', required: true)
json_dep = dependency('nlohmann_json', method: 'pkg-config', required: true)
openssl_dep = dependency('openssl', required: true)
sodium_dep = dependency('libsodium', required: true)
libpqx_dep = dependency('libpqxx', required: true)
redis_dep = dependency('redis++', required: true)

# Include directories
include_dirs = include_directories('include', 'test')

# Library target
bitfinex_gateway_lib = library('bitfinex_gateway',
  'src/Gateway.cpp',
  include_directories: include_dirs,
  dependencies: [json_dep, openssl_dep, sodium_dep, libpqx_dep,
  singular_dep, gateway_dep, redis_dep]
)

# Test executable
bitfinex_gateway_tests = executable('bitfinex_gateway_tests',
  'test/BitfinexGatewayTests.cpp',
  'test/SeedData.cpp',
  include_directories: include_dirs,
  dependencies: [gtest_dep, gmock_dep, gtest_main_dep, json_dep, openssl_dep, sodium_dep, libpqx_dep,
    singular_dep, gateway_dep, redis_dep],
  link_with: bitfinex_gateway_lib
)

# Register the test
test('gateway_tests', bitfinex_gateway_tests, protocol: 'gtest')
