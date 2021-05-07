/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* @brief Generate an HTTPS request in the format the positioning service expects.
 *
 * @param cell_data Pointer to neighbor cell data.
 * @param buf Buffer for storing the HTTP request.
 * @param buf_len Lenght of the provided buffer.
 *
 * @return 0 on success, or negative errror code on failure.
 */
int position_service_generete_request(struct lte_lc_cells_info *cell_data,
                                      char *buf, size_t buf_len);

/* @brief Get pointer to the service's null-terminated hostname.
 *
 * @return A pointer to null-terminated hostname in case of success,
 *         or NULL in case of failure.
 */
const char *position_service_get_hostname(void);

/* @brief Get pointer to certificate to positioning service.
 *
 * @return A pointer to null-terminated root CA certificate in case of success,
 *         or NULL in case of failure.
 */
const char *position_service_get_certificate(void);

#ifdef __cplusplus
}
#endif
