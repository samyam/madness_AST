include $(top_srcdir)/config/MakeGlobal.am


GITREV="GITREVISION=\"`git --git-dir=$(top_srcdir)/.git rev-parse HEAD`\" "
SVNREV="SVNREVISION=\"`svnversion $(top_srcdir)`\" "

AM_CPPFLAGS += -D$(GITREV) -D$(SVNREV)

bin_PROGRAMS = test_AST h2dft hedft hello hatom_energy h2 he tdse heat heat2 csqrt \
 sdf_shape_tester test_gmres tdse1d vnucso nonlinschro sininteg functionio \
 dataloadbal hatom_1d binaryop dielectric hehf 3dharmonic testsolver \
 testspectralprop dielectric_external_field mp2 tiny oep h2dynamic newsolver \
 tdhf nemo 
 
thisincludedir = $(includedir)/examples
thisinclude_HEADERS = molecularmask.h  nonlinsol.h spectralprop.h

AUTOMAKE_OPTIONS = subdir-objects

mp2_SOURCES = mp2.cc 

oep_SOURCES = oep.cc 

tiny_SOURCES = tiny.cc

dataloadbal_SOURCES = dataloadbal.cc

dielectric_external_field_SOURCES = dielectric_external_field.cc

testspectralprop_SOURCES = testspectralprop.cc

testsolver_SOURCES = testsolver.cc

tdhf_SOURCES = tdhf.cc  tdhf_CIS.cc

nemo_SOURCES = nemo.cc

newsolver_SOURCES = newsolver.cc

hedft_SOURCES = hedft.cc 
h2dft_SOURCES = h2dft.cc  
if MADNESS_HAS_LIBXC
hedft_SOURCES += ../../lib/chem/xcfunctional_libxc.cc
h2dft_SOURCES += ../../lib/chem/xcfunctional_libxc.cc
endif
if MADNESS_HAS_MADXC
hedft_SOURCES += ../DFcode/xcfunctional_madxc.cc 
h2dft_SOURCES += ../DFcode/xcfunctional_madxc.cc 
else
hedft_SOURCES += ../../lib/chem/xcfunctional_ldaonly.cc 
h2dft_SOURCES += ../../lib/chem/xcfunctional_ldaonly.cc 
endif

sininteg_SOURCES = sininteg.cc

3dharmonic_SOURCES = 3dharmonic.cc

functionio_SOURCES = functionio.cc

nonlinschro_SOURCES = nonlinschro.cc

heat_SOURCES = heat.cc

heat2_SOURCES = heat2.cc

hello_SOURCES = hello.cc

dielectric_SOURCES = dielectric.cc

hatom_energy_SOURCES = hatom_energy.cc

h2_SOURCES = h2.cc

test_AST_SOURCES = test_AST.cc
 
h2dynamic_SOURCES = h2dynamic.cc

he_SOURCES = he.cc

hehf_SOURCES = hehf.cc

# h2hf_SOURCES = h2hf.cc

tdse_SOURCES = tdse.cc

tdse1d_SOURCES = tdse1d.cc

csqrt_SOURCES = csqrt.cc

sdf_shape_tester_SOURCES = sdf_shape_tester.cc

test_gmres_SOURCES = test_gmres.cc

vnucso_SOURCES = vnucso.cc

hatom_1d_SOURCES = hatom_1d.cc

binaryop_SOURCES = binaryop.cc

LDADD = $(MRALIBS)

