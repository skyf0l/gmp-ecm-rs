/* LucasChainGen.h: header file for LucasChainGen, a minimal-length
   Lucas chain generator for prime numbers.

Copyright 2023, 2024 Paul Zimmermann, Philip McLaughlin.

This file is part of the ECM Library.

The ECM Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The ECM Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the ECM Library; see the file COPYING.LIB.  If not, see
http://www.gnu.org/licenses/ or write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA. */

#ifndef LUCASCHAINGEN_H_
#define LUCASCHAINGEN_H_

#define FIB_LIMIT 56				/* maximum # of Fibonacci numbers */
#define MAX_WORKING_CHAIN_LENGTH 64	/* maximum # of chain elements in the current working chain */
#define MAX_CODE_OR_PRIME_COUNT 6000000	/* maximum size for both chain code & target prime arrays */
#define MAX_CAND_LIST_COUNT 500		/* maximum total # of candidates in the recursive list */
#define MAX_CANDIDATE_COUNT 24		/* maximum number of candidates to extend any given chain */
#define MAX_THREADS 16
#define TOTAL_WORK_COUNT 50			/* number of work assignments */
#define DEFAULT_THREAD_COUNT 4

/* sieve parameters */
#define SBL 400						/* sieve bootstrap limit: all primes less than this limit
 	 	 	 	 	 	 	 	 	 	 are used to generate the sieve prime list */
#define SPL 160000				/* sieve prime list limit: all primes p such that 17 <= p < SPL will be sieve primes */
#define MAX_SIEVE_LIMIT 25600000000	/* integer limit above which composite integers will appear after sieving */
#define NEWSIEVE_SIZE 80000		/* byte sieve size to generate all sieve primes (SPL/2) */
#define SIEVE_SPACE_SIZE 500000		/* byte sieve size; (NOTE: must be >= NEWSIEVE_SIZE */
#define SIEVE_PRIME_COUNT 14700		/* array size estimate based on pi(SPL) approx = SPL/(ln(SPL) - 1.1) */
 	 	 	 	 	 	 	 	 	/* The true number of sieve primes is 14677 from 17 to 159979 */

/* chain start possibilities:
 1 2 3 4 5  c's = 6, 7, 8\, 9*
 1 2 3 4 6  c's = 7*, 8*, 9*, 10X
 1 2 3 4 7  c's = 8*, 10*, 11*
 1 2 3 5 6  c's = 7\, 8\, 9*, 10*, 11*
 1 2 3 5 7  c's = 8\, 9*, 10*, 12*
 1 2 3 5 8  c's = 10*, 11*, 13*
 \ ==> has appeared in a minimal length chain
 * ==> has appeared in a minimal length chain with maximum # of doubled elements
 X ==> only composite continuations
*/

/* 3-bit chain start sequences. Covers all possible Lucas chains */
#define CHAIN_START_5_8_13 0x7
#define CHAIN_START_5_8_11 0x6
#define CHAIN_START_5_8_10 0x5
#define CHAIN_START_5_7    0x4
#define CHAIN_START_5_6    0x3
#define CHAIN_START_4_7    0x2
#define CHAIN_START_4_5    0x1
#define CHAIN_START_4_6    0x0 /* precludes a completely zero code */

#define _true_ (1)
#define _false_ (0)

typedef struct
{
	uint32_t	prime;
	uint64_t	sieve_space_start_index;
	uint32_t	dif_table_start_index;

} sieve_params;

/* when a target prime is the n-th prime, save_index = n - 7. For example,
 *  for prime = 17, save_index = 0; for 19 save -index = 1, etc. */
typedef struct
{
	uint64_t	prime;
	uint32_t	save_index;

} target_prime;

typedef struct
{
	uint64_t	value;			/* integer value of this chain element */
	uint8_t	comp_offset_1;	/* larger summand (summand_1) component index counting back from parent (parent = 0) */
	uint8_t	comp_offset_2;	/* smaller summand (summand_2) component index counting back from parent */
	uint8_t	dif_offset;		/* component index of (summand_1 - summand_2) counting back from parent */
								/* note: dif_offset = 0 will indicate this is a doubled element */
	uint8_t	chain_dbl_count;	/* total # of doubled elements in the working chain up to and including this element */

} chain_element;

/* variables used by multiple routines, one copy for each thread */
typedef struct
{
/*	target_prime tgt_prime_list[MAX_CODE_OR_PRIME_COUNT]; */
	uint64_t chain_code_list[MAX_CODE_OR_PRIME_COUNT];
	uint32_t chain_count[MAX_CODE_OR_PRIME_COUNT];
	uint8_t chain_max_dbl_count[MAX_CODE_OR_PRIME_COUNT];
	uint16_t chain_count_max_dbls[MAX_CODE_OR_PRIME_COUNT];
	uint8_t tgt_prime_code_length[MAX_CODE_OR_PRIME_COUNT];
	chain_element working_chain[MAX_WORKING_CHAIN_LENGTH];
	uint64_t chain_values[MAX_WORKING_CHAIN_LENGTH];
	chain_element candidate_list[MAX_CAND_LIST_COUNT];
	uint64_t Fib[FIB_LIMIT];
	uint64_t Luc[FIB_LIMIT];
	chain_element raw_c_list[MAX_CANDIDATE_COUNT];
	uint8_t check_result[MAX_CANDIDATE_COUNT];
	uint8_t check_index;
	uint32_t chain_code_list_start_index;
	uint32_t tgt_p_count;
	uint8_t w_chain_length;
	uint8_t current_partial_length; /* current # of elements in the working chain */
	uint16_t c_list_start_index; /* next available slot in the candidate list */
	uint16_t current_c_index;
	double index_count_per_val;
	uint8_t code_length;

} mem_struct;

typedef struct
{
	chain_element working_chain[15];
	uint8_t current_partial_length; /* starting # of elements in the working chain */

} work_struct;

typedef struct
{
	uint8_t thrd_indx;

} thread_io_struct;

/* prototypes */

uint8_t	*get_dif_table_ptr(void);
uint8_t	*get_sieve_space_ptr(void);
sieve_params	*get_sieve_primes_ptr(void);
uint32_t	sieve_init(void);
void		standard_sieve(uint32_t);
uint32_t	prime_count( uint32_t *, uint32_t *);

uint64_t	cputime(void);
void		init_working_chains(void);
void 		set_work_assignments(void);
void		init_Fib_sequence(void);
void		init_Luc_sequence(void);
void		init_thread_memory(void);
void		consolidate_results(void);
void		*recursive_work(void *);

/* subroutines requiring templates */
void copy_work_assignment_to_thread( uint8_t );
void copy_work_assignment_to_thread_01( uint8_t );
void copy_work_assignment_to_thread_02( uint8_t );
void copy_work_assignment_to_thread_03( uint8_t );
void copy_work_assignment_to_thread_04( uint8_t );
void copy_work_assignment_to_thread_05( uint8_t );
void copy_work_assignment_to_thread_06( uint8_t );
void copy_work_assignment_to_thread_07( uint8_t );
void copy_work_assignment_to_thread_08( uint8_t );
void copy_work_assignment_to_thread_09( uint8_t );
void copy_work_assignment_to_thread_10( uint8_t );
void copy_work_assignment_to_thread_11( uint8_t );
void copy_work_assignment_to_thread_12( uint8_t );
void copy_work_assignment_to_thread_13( uint8_t );
void copy_work_assignment_to_thread_14( uint8_t );
void copy_work_assignment_to_thread_15( uint8_t );

uint64_t	encode_Lchain(void);
uint64_t	encode_Lchain_01(void);
uint64_t	encode_Lchain_02(void);
uint64_t	encode_Lchain_03(void);
uint64_t	encode_Lchain_04(void);
uint64_t	encode_Lchain_05(void);
uint64_t	encode_Lchain_06(void);
uint64_t	encode_Lchain_07(void);
uint64_t	encode_Lchain_08(void);
uint64_t	encode_Lchain_09(void);
uint64_t	encode_Lchain_10(void);
uint64_t	encode_Lchain_11(void);
uint64_t	encode_Lchain_12(void);
uint64_t	encode_Lchain_13(void);
uint64_t	encode_Lchain_14(void);
uint64_t	encode_Lchain_15(void);

uint8_t	not_divisible_by_3( uint64_t );
uint8_t	not_divisible_by_3_01( uint64_t );
uint8_t	not_divisible_by_3_02( uint64_t );
uint8_t	not_divisible_by_3_03( uint64_t );
uint8_t	not_divisible_by_3_04( uint64_t );
uint8_t	not_divisible_by_3_05( uint64_t );
uint8_t	not_divisible_by_3_06( uint64_t );
uint8_t	not_divisible_by_3_07( uint64_t );
uint8_t	not_divisible_by_3_08( uint64_t );
uint8_t	not_divisible_by_3_09( uint64_t );
uint8_t	not_divisible_by_3_10( uint64_t );
uint8_t	not_divisible_by_3_11( uint64_t );
uint8_t	not_divisible_by_3_12( uint64_t );
uint8_t	not_divisible_by_3_13( uint64_t );
uint8_t	not_divisible_by_3_14( uint64_t );
uint8_t	not_divisible_by_3_15( uint64_t );

uint8_t	not_divisible_by_5( uint64_t );
uint8_t	not_divisible_by_5_01( uint64_t );
uint8_t	not_divisible_by_5_02( uint64_t );
uint8_t	not_divisible_by_5_03( uint64_t );
uint8_t	not_divisible_by_5_04( uint64_t );
uint8_t	not_divisible_by_5_05( uint64_t );
uint8_t	not_divisible_by_5_06( uint64_t );
uint8_t	not_divisible_by_5_07( uint64_t );
uint8_t	not_divisible_by_5_08( uint64_t );
uint8_t	not_divisible_by_5_09( uint64_t );
uint8_t	not_divisible_by_5_10( uint64_t );
uint8_t	not_divisible_by_5_11( uint64_t );
uint8_t	not_divisible_by_5_12( uint64_t );
uint8_t	not_divisible_by_5_13( uint64_t );
uint8_t	not_divisible_by_5_14( uint64_t );
uint8_t	not_divisible_by_5_15( uint64_t );

uint8_t	extract_chain_values(void);
uint8_t	extract_chain_values_01(void);
uint8_t	extract_chain_values_02(void);
uint8_t	extract_chain_values_03(void);
uint8_t	extract_chain_values_04(void);
uint8_t	extract_chain_values_05(void);
uint8_t	extract_chain_values_06(void);
uint8_t	extract_chain_values_07(void);
uint8_t	extract_chain_values_08(void);
uint8_t	extract_chain_values_09(void);
uint8_t	extract_chain_values_10(void);
uint8_t	extract_chain_values_11(void);
uint8_t	extract_chain_values_12(void);
uint8_t	extract_chain_values_13(void);
uint8_t	extract_chain_values_14(void);
uint8_t	extract_chain_values_15(void);

void		copy_candidate_to_working_chain(void);
void		copy_candidate_to_working_chain_01(void);
void		copy_candidate_to_working_chain_02(void);
void		copy_candidate_to_working_chain_03(void);
void		copy_candidate_to_working_chain_04(void);
void		copy_candidate_to_working_chain_05(void);
void		copy_candidate_to_working_chain_06(void);
void		copy_candidate_to_working_chain_07(void);
void		copy_candidate_to_working_chain_08(void);
void		copy_candidate_to_working_chain_09(void);
void		copy_candidate_to_working_chain_10(void);
void		copy_candidate_to_working_chain_11(void);
void		copy_candidate_to_working_chain_12(void);
void		copy_candidate_to_working_chain_13(void);
void		copy_candidate_to_working_chain_14(void);
void		copy_candidate_to_working_chain_15(void);

uint16_t	gen_candidate_list(void);
uint16_t	gen_candidate_list_01(void);
uint16_t	gen_candidate_list_02(void);
uint16_t	gen_candidate_list_03(void);
uint16_t	gen_candidate_list_04(void);
uint16_t	gen_candidate_list_05(void);
uint16_t	gen_candidate_list_06(void);
uint16_t	gen_candidate_list_07(void);
uint16_t	gen_candidate_list_08(void);
uint16_t	gen_candidate_list_09(void);
uint16_t	gen_candidate_list_10(void);
uint16_t	gen_candidate_list_11(void);
uint16_t	gen_candidate_list_12(void);
uint16_t	gen_candidate_list_13(void);
uint16_t	gen_candidate_list_14(void);
uint16_t	gen_candidate_list_15(void);

void		check_candidate(void);
void		check_candidate_01(void);
void		check_candidate_02(void);
void		check_candidate_03(void);
void		check_candidate_04(void);
void		check_candidate_05(void);
void		check_candidate_06(void);
void		check_candidate_07(void);
void		check_candidate_08(void);
void		check_candidate_09(void);
void		check_candidate_10(void);
void		check_candidate_11(void);
void		check_candidate_12(void);
void		check_candidate_13(void);
void		check_candidate_14(void);
void		check_candidate_15(void);

/* uint8_t	generate_Lchain( uint64_t, uint64_t, chain_element *, uint8_t *, uint8_t *, uint32_t * ); */
/* void		max_continuation( chain_element *, uint8_t *, uint8_t ); */

void		generate_and_process_candidate_list(void);
void		generate_and_process_candidate_list_01(void);
void		generate_and_process_candidate_list_02(void);
void		generate_and_process_candidate_list_03(void);
void		generate_and_process_candidate_list_04(void);
void		generate_and_process_candidate_list_05(void);
void		generate_and_process_candidate_list_06(void);
void		generate_and_process_candidate_list_07(void);
void		generate_and_process_candidate_list_08(void);
void		generate_and_process_candidate_list_09(void);
void		generate_and_process_candidate_list_10(void);
void		generate_and_process_candidate_list_11(void);
void		generate_and_process_candidate_list_12(void);
void		generate_and_process_candidate_list_13(void);
void		generate_and_process_candidate_list_14(void);
void		generate_and_process_candidate_list_15(void);

#endif /* LUCASCHAINGEN_H_ */
