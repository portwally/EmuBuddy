/* Slot cards with their own firmware: the interface between a card and the
 * IIgs memory map (moremem.c).
 *
 * A card registered with slot_card_set() gets:
 *   $C0n0-$C0nF  (n = slot + 8)  its 16 I/O registers (DEVSEL)
 *   $Cs00-$CsFF                  its slot ROM page (IOSEL), when the slot is set
 *                                to "Your Card" ($C02D) and INTCXROM is off
 *   $C800-$CFFF                  its expansion ROM space (IOSTROBE), from the
 *                                moment its $Cs00 page is read or written until
 *                                $CFFF is touched, as on real Apple II hardware
 * The internal ROM still wins over $C800-$CFFF when INTCXROM or INTC8ROM is set.
 *
 * Reads of these areas go through io_read(), so a card can bank its ROM and
 * RAM freely without telling the page tables. */
#ifndef EMUBUDDY_SLOTCARD_H
#define EMUBUDDY_SLOTCARD_H

typedef struct Slot_card {
	const char *name;
	/* reg = 0-15 for $C0n0-$C0nF */
	unsigned (*io_read)(int reg, unsigned long long dfcyc);
	void	(*io_write)(int reg, unsigned val, unsigned long long dfcyc);
	/* off = 0-255 for $Cs00-$CsFF */
	unsigned (*rom_read)(int off, unsigned long long dfcyc);
	/* off = 0-0x7ff for $C800-$CFFF (exp_read may be NULL: no
	 * expansion ROM, the floating bus is read) */
	unsigned (*exp_read)(int off, unsigned long long dfcyc);
	void	(*exp_write)(int off, unsigned val, unsigned long long dfcyc);
	void	(*reset)(void);
} Slot_card;

/* Plug a card into slot 1-7 (NULL removes it). Safe while running. */
void	slot_card_set(int slot, Slot_card *card);
Slot_card *slot_card_get(int slot);
/* The slot that owns $C800-$CFFF now (0 = none). */
int	slot_card_c800_owner(void);
/* Called from do_reset(): releases $C800 and resets every card. */
void	slot_cards_reset(void);

#endif
