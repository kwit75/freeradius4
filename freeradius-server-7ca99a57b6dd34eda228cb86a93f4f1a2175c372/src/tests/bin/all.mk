TEST	:= test.bin

#FILES	:= \
#	atomic_queue_test 	\
#	dhcpclient		\
#	message_set_test	\
#	radclient		\
#	radict 			\
#	radmin			\
#	radsniff 		\
#	radsnmp 		\
#	radwho 			\
#	rbmonkey 		\
#	ring_buffer_test 	\
#	rlm_redis_ippool_tool 	\
#	smbencrypt 		\
#	unit_test_attribute 	\
#	unit_test_map 		\
#	unit_test_module

$(eval $(call TEST_BOOTSTRAP))

#
#  Files in the output dir depend on the bin tests, and on the binary
#  that we're running
#
$(BUILD_DIR)/tests/bin/%: $(DIR)/% $(BUILD_DIR)/bin/% $(BUILD_DIR)/bin/local/%
	@echo "BIN-TEST $(notdir $@)"
	${Q}if ! TEST_BIN="$(TEST_BIN)" DICT_DIR="$(top_srcdir)/share/dictionary" $<; then \
		echo TEST_BIN=\"$(TEST_BIN)\" DICT_DIR="$(top_srcdir)/share/dictionary" $<; \
		exit 1; \
	fi
	${Q}touch $@
