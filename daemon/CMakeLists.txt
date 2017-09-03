cmake_minimum_required(VERSION 2.6)
INCLUDE(CheckLibraryExists)

PROJECT(avr-door-controller-daemon)
ADD_DEFINITIONS(-Os -Wall -g3)

ADD_EXECUTABLE(avr-door-controller-daemon
	avr-door-controller-daemon.c
	avr-door-controller-methods.c
	avr-door-controller-uart-transport.c)
TARGET_LINK_LIBRARIES(avr-door-controller-daemon ubus ubox)

INSTALL(TARGETS avr-door-controller-daemon
        RUNTIME DESTINATION bin
)

INSTALL(PROGRAMS avr-door-controller-daemon.rc
	DESTINATION /etc/init.d
	RENAME avr-door-controller-daemon
)

INSTALL(FILES avr-door-controller-daemon.acl
	DESTINATION /usr/share/rpcd/acl.d
	RENAME avr-door-controller-daemon.json
)
