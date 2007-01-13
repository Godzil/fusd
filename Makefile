#####
#
#  Makefile for FUSD
#
PREFIX = /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include

CC      = gcc 
LD      = gcc
INSTALL = install
STRIP   = strip
PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
ETCDIR  = /etc/$(TARGET)
MANDIR  = $(PREFIX)/man

GCF  = -std=gnu99 -O2 -g -I../include
SCF  = -fPIC
SLF  = -shared -nostdlib

export

####################################################

SUBDIRS = kfusd libfusd examples

default: 
	$(MAKE) -C libfusd 
	$(MAKE) -C kfusd
	$(MAKE) -C examples

install:
	$(MAKE) -C libfusd install
	$(MAKE) -C kfusd install
	$(MAKE) -C examples install

clean:
	$(MAKE) -C kfusd clean
	$(MAKE) -C libfusd clean
	$(MAKE) -C examples clean
