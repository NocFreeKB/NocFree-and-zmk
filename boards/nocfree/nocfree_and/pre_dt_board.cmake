# SPDX-License-Identifier: MIT

# Nordic SoC descriptions intentionally share peripheral unit addresses.
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
