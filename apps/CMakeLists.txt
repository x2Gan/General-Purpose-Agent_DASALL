add_subdirectory(cli)
add_subdirectory(daemon)
add_subdirectory(gateway)
add_subdirectory(simulator)

# v1 package surface installs only the local CLI and daemon entrypoints.
install(TARGETS dasall-cli
	RUNTIME DESTINATION ${DASALL_INSTALL_BINDIR}
)

install(TARGETS dasall-daemon
	RUNTIME DESTINATION ${DASALL_INSTALL_SBINDIR}
)
