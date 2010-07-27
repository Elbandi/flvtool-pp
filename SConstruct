libd = '#lib/'
env = Environment(CPPFLAGS='-ggdb -O3 -Wall', LINKFLAGS='-ggdb')
env.TargetSignatures('content')

Export('env libd')
SConscript('SConscript')
