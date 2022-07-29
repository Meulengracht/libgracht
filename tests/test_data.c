/**
 * Copyright 2021, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Gracht Testing Suite
 * - Implementation of various test programs that verify behaviour of libgracht
 */

#include <stdio.h>
#include <test_utils_service.h>

struct test_payment g_testPayment_19 = {
    .id = 19,
    .amount = 100.0
};

int test_verify_payment(const struct test_payment* obtained, const struct test_payment* expected)
{
    if (obtained->id != expected->id) {
        fprintf(stderr, "Expected payment id %d, got %d\n", expected->id, obtained->id);
        return -1;
    }
    if (obtained->amount != expected->amount) {
        fprintf(stderr, "Expected payment amount %d, got %d\n", expected->amount, obtained->amount);
        return -1;
    }
    return 0;
}

struct test_payment g_testPayments[] = {
    { .id = 1, .amount = -20 },
    { .id = 2, .amount = -3 },
    { .id = 3, .amount = -11 },
    { .id = 4, .amount = -15 },
    { .id = 5, .amount = -30 },
};

struct test_account g_testAccount_JohnDoe = {
    .id = 39858686,
    .name = "primary account",
    .balance = 100,
    .payments_count = 5,
    .payments = g_testPayments,
    .owner.type_type = 2,

    .owner.type.p.id = 39059895,
    .owner.type.p.name = "John Doe"
};

int test_verify_account(const struct test_account* obtained, const struct test_account* expected)
{
    if (obtained->id != expected->id) {
        fprintf(stderr, "Expected account id %d, got %d\n", expected->id, obtained->id);
        return -1;
    }
    if (strcmp(obtained->name, expected->name)) {
        fprintf(stderr, "Expected account name %s, got %s\n", expected->name, obtained->name);
        return -1;
    }
    if (obtained->balance != expected->balance) {
        fprintf(stderr, "Expected account balance %d, got %d\n", expected->balance, obtained->balance);
        return -1;
    }
    if (obtained->payments_count != expected->payments_count) {
        fprintf(stderr, "Expected account payments count %d, got %d\n", expected->payments_count, obtained->payments_count);
        return -1;
    }
    for (int i = 0; i < obtained->payments_count; i++) {
        if (test_verify_payment(&obtained->payments[i], &expected->payments[i]) != 0) {
            fprintf(stderr, "Expected account payment %d, got %d\n", expected->payments[i].id, obtained->payments[i].id);
            return -1;
        }
    }
    return 0;
}
