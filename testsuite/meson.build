libspelling_test_env = [
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

libspelling_testsuite_c_args = [
  '-DG_LOG_DOMAIN="libspelling"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

libspelling_testsuite = {
  'test-cursor' : {},
}

libspelling_testsuite_deps = [
  libspelling_static_dep,
]

foreach test, params: libspelling_testsuite
  test_exe = executable(test,
    ['@0@.c'.format(test)],
          c_args: libspelling_testsuite_c_args,
    dependencies: libspelling_testsuite_deps,
  )
  if not params.get('skip', false)
    test(test, test_exe, env: libspelling_test_env)
  endif
endforeach