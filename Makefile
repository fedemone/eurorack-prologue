PLATFORMDIR = $(INSTALLDIR)/$(platform)

.PHONY: all clean install

all: prologue minilogue-xd nutekt-digital drumlogue
	@echo "Build complete"

prologue: 
	@echo "Building prologue"
	$(MAKE) -f prologue.mk clean
	$(MAKE) -f prologue.mk
	$(MAKE) -f prologue.mk install INSTALLDIR=$(INSTALLDIR)

drumlogue:
	@echo "Building drumlogue"
	$(MAKE) -f drumlogue.mk clean
	$(MAKE) -f drumlogue.mk
	$(MAKE) -f drumlogue.mk install INSTALLDIR=$(INSTALLDIR)

minilogue-xd: 
	@echo "Building minilogue-xd"
	$(MAKE) -f minilogue-xd.mk clean
	$(MAKE) -f minilogue-xd.mk
	$(MAKE) -f minilogue-xd.mk install INSTALLDIR=$(INSTALLDIR)

nutekt-digital: 
	@echo "Building nutekt-digital"
	$(MAKE) -f nutekt-digital.mk clean
	$(MAKE) -f nutekt-digital.mk
	$(MAKE) -f nutekt-digital.mk install INSTALLDIR=$(INSTALLDIR)

clean: 
	$(MAKE) -f prologue.mk clean
	$(MAKE) -f minilogue-xd.mk clean
	$(MAKE) -f nutekt-digital.mk clean
	$(MAKE) -f drumlogue.mk clean

install: all
	@echo "Installing to: " $(INSTALLDIR)

package_prologue:
	cd $(INSTALLDIR)/prologue && zip -r ../../prologue.zip .

package_minilogue-xd:
	cd $(INSTALLDIR)/minilogue-xd && zip -r ../../minilogue-xd.zip .

package_nutekt-digital:
	cd $(INSTALLDIR)/nutekt-digital && zip -r ../../nutekt-digital.zip .

package_drumlogue:
	cd $(INSTALLDIR)/drumlogue && zip -r ../../drumlogue.zip .
