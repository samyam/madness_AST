SUBDIRS = world tinyxml muParser misc tensor mra chem

libraries:	$(lib_LIBRARIES)
	for dir in $(SUBDIRS) ; do $(MAKE) -C $$dir $@ ; done

install-libraries:
	for dir in $(SUBDIRS) ; do $(MAKE) -C $$dir $@ ; done
