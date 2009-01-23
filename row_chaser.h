/*
 * row_chaser.h
 */

#ifndef ROW_CHASER_H_
#define ROW_CHASER_H_

#include <iostream>
#include "seqan/sequence.h"
#include "ebwt.h"

/**
 * A class that statefully converts a row index to a reference
 * location.  There is a large memory-latency penalty usually
 * associated with calling the Ebwt object's mapLF method, which this
 * object does repeatedly in order to resolve the reference offset.
 * The "statefulness" in how the computation is organized here allows
 * some or all of that penalty to be hidden using prefetching.
 */
template<typename TStr>
class RowChaser {

	typedef std::pair<uint32_t,uint32_t> U32Pair;
	typedef Ebwt<TStr> EbwtT;

public:
	RowChaser(const EbwtT& ebwt) :
		prepped_(false),
		ebwt_(ebwt),
		qlen_(0),
		eh_(ebwt._eh),
		offMask_(ebwt_._eh._offMask),
		offRate_(ebwt_._eh._offRate),
		zOff_(ebwt_._zOff),
		offs_(ebwt_._offs),
		ebwtPtr_(ebwt_._ebwt),
		row_(0xffffffff),
		jumps_(0),
		sideloc_(),
		done_(false),
		off_(0xffffffff),
		tlen_(0)
	{ }

	/**
	 * Convert a row to a joined reference offset.  This has to be
	 * converted to understand where it is w/r/t the reference hit and
	 * offset within it.
	 */
	static uint32_t toFlatRefOff(const EbwtT& ebwt, uint32_t qlen, uint32_t row) {
		RowChaser rc(ebwt);
		rc.setRow(row, qlen);
		while(!rc.done()) {
			rc.advance();
		}
		return rc.flatOff();
	}

	/**
	 * Convert a row to a reference offset.
	 */
	static U32Pair toRefOff(const EbwtT& ebwt, uint32_t qlen, uint32_t row) {
		RowChaser rc(ebwt);
		rc.setRow(row, qlen);
		while(!rc.done()) {
			rc.advance();
		}
		return rc.off();
	}

	/**
	 * Set the next row for us to "chase" (i.e. map to a reference
	 * location using the BWT step-left operation).
	 */
	void setRow(uint32_t row, uint32_t qlen) {
		assert_neq(0xffffffff, row);
		assert_gt(qlen, 0);
		row_ = row;
		qlen_ = qlen;
		ASSERT_ONLY(sideloc_.invalidate());
		if(row_ == zOff_) {
			// We arrived at the extreme left-hand end of the reference
			off_ = 0;
			done_ = true;
			return;
		} else if((row_ & offMask_) == row_) {
			// We arrived at a marked row
			off_ = offs_[row_ >> offRate_];
			done_ = true;
			return;
		}
		done_ = false;
		jumps_ = 0;
		off_ = 0xffffffff;
		prepped_ = false;
		prep();
	}

	/**
	 * Return true iff off_ now holds the reference location
	 * corresponding to the row last set with setRow().
	 */
	bool done() const {
		return done_;
	}

	/**
	 * Advance the step-left process by one step.  Check if we're done.
	 */
	void advance() {
		// Advance by 1
		assert(!done_);
		assert(prepped_);
		prepped_ = false;
		uint32_t newrow = ebwt_.mapLF(sideloc_);
		ASSERT_ONLY(sideloc_.invalidate());
		jumps_++;
		assert_neq(newrow, row_);
		// Update row_ field
		row_ = newrow;
		if(row_ == zOff_) {
			// We arrived at the extreme left-hand end of the reference
			off_ = jumps_;
			done_ = true;
		} else if((row_ & offMask_) == row_) {
			// We arrived at a marked row
			off_ = offs_[row_ >> offRate_] + jumps_;
			done_ = true;
		}
		prep();
	}

	/**
	 * Prepare for the next call to advance() by prefetching the
	 * appropriate portions of the index.  The caller should make sure
	 * that the
	 */
	void prep() {
		if(!done_) {
			assert(!prepped_);
			assert(!sideloc_.valid());
			sideloc_.initFromRow(row_, eh_, ebwtPtr_);
			sideloc_.prefetch();
			assert(sideloc_.valid());
		}
		prepped_ = true;
	}

	/**
	 * Get the calculated offset.  This has to be converted with a call
	 * to Ebwt::joinedToTextOff() to understand where it is w/r/t the
	 * reference hit and offset within it.
	 */
	uint32_t flatOff() const {
		return off_;
	}

	/**
	 * Get the calculated offset.
	 */
	U32Pair off() {
		uint32_t off = flatOff();
		assert_neq(0xffffffff, off);
		uint32_t tidx;
		uint32_t textoff;
		ebwt_.joinedToTextOff(qlen_, off, tidx, textoff, tlen_);
		// Note: tidx may be 0xffffffff, if alignment overlaps a
		// reference boundary
		return make_pair(tidx, textoff);
	}

	uint32_t tlen() const {
		return tlen_;
	}

	bool prepped_; /// true = prefetch is issued and it's OK to call advance()

protected:

	const EbwtT& ebwt_;      /// index to resolve row in
	uint32_t qlen_;          /// length of read; needed to convert to ref. coordinates
	const EbwtParams& eh_;   /// eh field from index
	const uint32_t offMask_; /// offMask field from index
	const uint32_t offRate_; /// offRate field from index
	const uint32_t zOff_;    /// zOff field from index
	const uint32_t* offs_;   /// ptr to offs[] array from index
	const uint8_t* ebwtPtr_; /// ptr to ebwt[] array from index
	uint32_t row_;           /// current row
	uint32_t jumps_;         /// # steps so far
	SideLocus sideloc_;      /// current side locus
	bool done_;              /// true = chase is done & answer is in off_
	uint32_t off_;           /// calculated offset (0xffffffff if not done)
	uint32_t tlen_;          /// hit text length
};

#endif /* ROW_CHASER_H_ */