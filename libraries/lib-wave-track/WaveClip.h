/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveClip.h

  ?? Dominic Mazzoni
  ?? Markus Meyer

*******************************************************************/

#ifndef __AUDACITY_WAVECLIP__
#define __AUDACITY_WAVECLIP__



#include "ClientData.h"
#include "SampleFormat.h"
#include "XMLTagHandler.h"
#include "SampleCount.h"

#include <wx/longlong.h>

#include <cassert>
#include <vector>
#include <functional>

class BlockArray;
class Envelope;
class ProgressDialog;
class sampleCount;
class SampleBlock;
class SampleBlockFactory;
using SampleBlockFactoryPtr = std::shared_ptr<SampleBlockFactory>;
class Sequence;
class wxFileNameWrapper;
namespace BasicUI { class ProgressDialog; }

class WaveClip;

// Array of pointers that assume ownership
using WaveClipHolder = std::shared_ptr< WaveClip >;
using WaveClipHolders = std::vector < WaveClipHolder >;
using WaveClipConstHolders = std::vector < std::shared_ptr< const WaveClip > >;

// A bundle of arrays needed for drawing waveforms.  The object may or may not
// own the storage for those arrays.  If it does, it destroys them.
class WaveDisplay
{
public:
   int width;
   sampleCount *where;
   float *min, *max, *rms;

   std::vector<sampleCount> ownWhere;
   std::vector<float> ownMin, ownMax, ownRms;

public:
   WaveDisplay(int w)
      : width(w), where(0), min(0), max(0), rms(0)
   {
   }

   // Create "own" arrays.
   void Allocate()
   {
      ownWhere.resize(width + 1);
      ownMin.resize(width);
      ownMax.resize(width);
      ownRms.resize(width);

      where = &ownWhere[0];
      if (width > 0) {
         min = &ownMin[0];
         max = &ownMax[0];
         rms = &ownRms[0];
      }
      else {
         min = max = rms = 0;
      }
   }

   ~WaveDisplay()
   {
   }
};

struct WAVE_TRACK_API WaveClipListener
{
   virtual ~WaveClipListener() = 0;
   virtual void MarkChanged() = 0;
   virtual void Invalidate() = 0;
};

class WAVE_TRACK_API WaveClip final : public XMLTagHandler
   , public ClientData::Site< WaveClip, WaveClipListener >
{
private:
   // It is an error to copy a WaveClip without specifying the
   // sample block factory.

   WaveClip(const WaveClip&) PROHIBITED;
   WaveClip& operator= (const WaveClip&) PROHIBITED;

public:
   using Caches = Site< WaveClip, WaveClipListener >;

   //! typical constructor
   /*!
    @param width how many sequences
    @pre `width > 0`
    @post `GetWidth() == width`
    */
   WaveClip(size_t width,
      const SampleBlockFactoryPtr &factory, sampleFormat format,
      int rate, int colourIndex);

   //! essentially a copy constructor - but you must pass in the
   //! current sample block factory, because we might be copying
   //! from one project to another
   /*!
    @post `GetWidth() == orig.GetWidth()`
    */
   WaveClip(const WaveClip& orig,
            const SampleBlockFactoryPtr &factory,
            bool copyCutlines);

   //! @brief Copy only a range from the given WaveClip
   /*!
    @pre CountSamples(t1, t0) > 0
    @post `GetWidth() == orig.GetWidth()`
    */
   WaveClip(const WaveClip& orig,
            const SampleBlockFactoryPtr &factory,
            bool copyCutlines,
            double t0, double t1);

   virtual ~WaveClip();

   //! Check invariant conditions on mSequences and mCutlines
   bool CheckInvariants() const;

   //! How many Sequences the clip contains.
   //! Set at construction time; changes only if increased by deserialization
   size_t GetWidth() const;

   void ConvertToSampleFormat(sampleFormat format,
      const std::function<void(size_t)> & progressReport = {});

   // Always gives non-negative answer, not more than sample sequence length
   // even if t0 really falls outside that range
   sampleCount TimeToSequenceSamples(double t) const;
   sampleCount ToSequenceSamples(sampleCount s) const;

   int GetRate() const { return mRate; }

   // Set rate without resampling. This will change the length of the clip
   void SetRate(int rate);

   // Resample clip. This also will set the rate, but without changing
   // the length of the clip
   void Resample(int rate, BasicUI::ProgressDialog *progress = NULL);

   void SetColourIndex( int index ){ mColourIndex = index;};
   int GetColourIndex( ) const { return mColourIndex;};
   
   double GetSequenceStartTime() const noexcept;
   void SetSequenceStartTime(double startTime);
   double GetSequenceEndTime() const;
   //! Returns the index of the first sample of the underlying sequence
   sampleCount GetSequenceStartSample() const;
   //! Returns the index of the sample next after the last sample of the underlying sequence
   sampleCount GetSequenceEndSample() const;
   //! Returns the total number of samples in all underlying sequences
   //! (but not counting the cutlines)
   sampleCount GetSequenceSamplesCount() const;

   double GetPlayStartTime() const noexcept;
   void SetPlayStartTime(double time);

   double GetPlayEndTime() const;

   sampleCount GetPlayStartSample() const;
   sampleCount GetPlayEndSample() const;
   sampleCount GetPlaySamplesCount() const;

   //! Sets the play start offset in seconds from the beginning of the underlying sequence
   void SetTrimLeft(double trim);
   //! Returns the play start offset in seconds from the beginning of the underlying sequence
   double GetTrimLeft() const noexcept;

   //! Sets the play end offset in seconds from the ending of the underlying sequence
   void SetTrimRight(double trim);
   //! Returns the play end offset in seconds from the ending of the underlying sequence
   double GetTrimRight() const noexcept;

   //! Moves play start position by deltaTime
   void TrimLeft(double deltaTime);
   //! Moves play end position by deltaTime
   void TrimRight(double deltaTime);

   //! Sets the the left trimming to the absolute time (if that is in bounds)
   void TrimLeftTo(double to);
   //! Sets the the right trimming to the absolute time (if that is in bounds)
   void TrimRightTo(double to);

   /*! @excsafety{No-fail} */
   void Offset(double delta) noexcept;

   // One and only one of the following is true for a given t (unless the clip
   // has zero length -- then BeforePlayStartTime() and AfterPlayEndTime() can both be true).
   // WithinPlayRegion() is true if the time is substantially within the clip
   bool WithinPlayRegion(double t) const;
   bool BeforePlayStartTime(double t) const;
   bool AfterPlayEndTime(double t) const;

   //! Counts number of samples within t0 and t1 region. t0 and t1 are
   //! rounded to the nearest clip sample boundary, i.e. relative to clips
   //! start time offset.
   //! @returns Number of samples within t0 and t1 if t1 > t0, 0 otherwise
   sampleCount CountSamples(double t0, double t1) const;

   //! Get samples from one channel
   /*!
    @param ii identifies the channel
    @pre `ii < GetWidth()`
    */
   bool GetSamples(size_t ii, samplePtr buffer, sampleFormat format,
                   sampleCount start, size_t len, bool mayThrow = true) const;

   //! Get (non-interleaved) samples from all channels
   /*!
    assume as many buffers available as GetWidth()
    */
   bool GetSamples(samplePtr buffers[], sampleFormat format,
                   sampleCount start, size_t len, bool mayThrow = true) const;

   //! @param ii identifies the channel
   /*!
    @pre `ii < GetWidth()`
    */
   void SetSamples(size_t ii, constSamplePtr buffer, sampleFormat format,
      sampleCount start, size_t len,
      sampleFormat effectiveFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   );

   Envelope* GetEnvelope() { return mEnvelope.get(); }
   const Envelope* GetEnvelope() const { return mEnvelope.get(); }

   //! @param ii identifies the channel
   /*!
    @pre `ii < GetWidth()`
    */
   BlockArray* GetSequenceBlockArray(size_t ii);
   /*!
    @copydoc GetSequenceBlockArray
    */
   const BlockArray* GetSequenceBlockArray(size_t ii) const;

   //! Get low-level access to a sequence. Whenever possible, don't use this,
   //! but use more high-level functions inside WaveClip (or add them if you
   //! think they are useful for general use)
   /*!
    @pre `ii < GetWidth()`
    */
   Sequence* GetSequence(size_t ii) {
      assert(ii < GetWidth());
      return mSequences[ii].get();
   }
   /*!
    @copydoc GetSequence
    */
   const Sequence* GetSequence(size_t ii) const { return mSequences[ii].get(); }

   /** WaveTrack calls this whenever data in the wave clip changes. It is
    * called automatically when WaveClip has a chance to know that something
    * has changed, like when member functions SetSamples() etc. are called. */
   /*! @excsafety{No-fail} */
   void MarkChanged();

   /** Getting high-level data for one channel for screen display and clipping
    * calculations and Contrast */
   /*!
    @param ii identifies the channel
    @pre `ii < GetWidth()`
    */
   std::pair<float, float> GetMinMax(size_t ii,
      double t0, double t1, bool mayThrow) const;
   /*!
    @copydoc GetMinMax
    */
   float GetRMS(size_t ii, double t0, double t1, bool mayThrow) const;

   /** Whenever you do an operation to the sequence that will change the number
    * of samples (that is, the length of the clip), you will want to call this
    * function to tell the envelope about it. */
   void UpdateEnvelopeTrackLen();

   //! For use in importing pre-version-3 projects to preserve sharing of blocks; no dithering applied
   //! @pre `GetWidth() == 1`
   std::shared_ptr<SampleBlock> AppendNewBlock(
      samplePtr buffer, sampleFormat format, size_t len);

   //! For use in importing pre-version-3 projects to preserve sharing of blocks
   //! @pre `GetWidth() == 1`
   void AppendSharedBlock(const std::shared_ptr<SampleBlock> &pBlock);

   //! Append (non-interleaved) samples to all channels
   //! You must call Flush after the last Append
   /*!
    @return true if at least one complete block was created
    assume as many buffers available as GetWidth()
    In case of failure or exceptions, the clip contents are unchanged but
    un-flushed data are lost
    */
   bool Append(constSamplePtr buffers[], sampleFormat format,
      size_t len, unsigned int stride,
      sampleFormat effectiveFormat /*!<
         Make the effective format of the data at least the minumum of this
         value and `format`.  (Maybe wider, if merging with preexistent data.)
         If the data are later narrowed from stored format, but not narrower
         than the effective, then no dithering will occur.
      */
   );

   //! Flush must be called after last Append
   /*!
    In case of exceptions, the clip contents are unchanged but
    un-flushed data are lost
    */
   void Flush();

   /// This name is consistent with WaveTrack::Clear. It performs a "Cut"
   /// operation (but without putting the cut audio to the clipboard)
   void Clear(double t0, double t1);

   /// Removes samples starting from the left boundary of the clip till
   /// t, if it's inside the play region. Also removes trimmed (hidden)
   /// data, if present. Changes offset to make remaining samples stay
   /// at their old place. Destructive operation.
   void ClearLeft(double t);
   /// Removes samples starting from t (if it's inside the clip),
   /// till the right boundary. Also removes trimmed (hidden)
   /// data, if present. Destructive operation.
   void ClearRight(double t);

   /// Clear, and add cut line that starts at t0 and contains everything until t1
   /// if there is at least one clip sample between t0 and t1, noop otherwise.
   void ClearAndAddCutLine(double t0, double t1);

   //! Paste data from other clip, resampling it if not equal rate
   /*!
    @return true and succeed if and only if `this->GetWidth() == other.GetWidth()`
    */
   bool Paste(double t0, const WaveClip &other);

   /** Insert silence - note that this is an efficient operation for large
    * amounts of silence */
   void InsertSilence( double t, double len, double *pEnvelopeValue = nullptr );

   /** Insert silence at the end, and causes the envelope to ramp
       linearly to the given value */
   void AppendSilence( double len, double envelopeValue );

   /// Get access to cut lines list
   WaveClipHolders &GetCutLines() { return mCutLines; }
   const WaveClipConstHolders &GetCutLines() const
      { return reinterpret_cast< const WaveClipConstHolders& >( mCutLines ); }
   size_t NumCutLines() const { return mCutLines.size(); }

   /** Find cut line at (approximately) this position. Returns true and fills
    * in cutLineStart and cutLineEnd (if specified) if a cut line at this
    * position could be found. Return false otherwise. */
   bool FindCutLine(double cutLinePosition,
                    double* cutLineStart = NULL,
                    double *cutLineEnd = NULL) const;

   /** Expand cut line (that is, re-insert audio, then DELETE audio saved in
    * cut line). Returns true if a cut line could be found and successfully
    * expanded, false otherwise */
   void ExpandCutLine(double cutLinePosition);

   /// Remove cut line, without expanding the audio in it
   bool RemoveCutLine(double cutLinePosition);

   /// Offset cutlines right to time 't0' by time amount 'len'
   void OffsetCutLines(double t0, double len);

   //! Should be called upon project close.  Not balanced by unlocking calls.
   /*! @excsafety{No-fail} */
   void CloseLock() noexcept;

   //
   // XMLTagHandler callback methods for loading and saving
   //

   bool HandleXMLTag(const std::string_view& tag, const AttributesList &attrs) override;
   void HandleXMLEndTag(const std::string_view& tag) override;
   XMLTagHandler *HandleXMLChild(const std::string_view& tag) override;
   void WriteXML(XMLWriter &xmlFile) const /* not override */;

   // AWD, Oct 2009: for pasting whitespace at the end of selection
   bool GetIsPlaceholder() const { return mIsPlaceholder; }
   void SetIsPlaceholder(bool val) { mIsPlaceholder = val; }

   // used by commands which interact with clips using the keyboard
   bool SharesBoundaryWithNextClip(const WaveClip* next) const;

   void SetName(const wxString& name);
   const wxString& GetName() const;

   sampleCount TimeToSamples(double time) const noexcept;
   double SamplesToTime(sampleCount s) const noexcept;

   //! Silences the 'length' amount of samples starting from 'offset'(relative to the play start)
   void SetSilence(sampleCount offset, sampleCount length);

   //! Get one channel of the append buffer
   /*!
    @param ii identifies the channel
    @pre `ii < GetWidth()`
    */
   constSamplePtr GetAppendBuffer(size_t ii) const;
   size_t GetAppendBufferLen() const;

private:
   sampleCount GetNumSamples() const;
   SampleFormats GetSampleFormats() const;
   const SampleBlockFactoryPtr &GetFactory();

   /// This name is consistent with WaveTrack::Clear. It performs a "Cut"
   /// operation (but without putting the cut audio to the clipboard)
   void ClearSequence(double t0, double t1);

   //! Restores state when an update loop over mSequences fails midway
   struct Transaction {
      explicit Transaction(WaveClip &clip);
      ~Transaction();
      void Commit() { committed = true; }

      WaveClip &clip;
      std::vector<std::unique_ptr<Sequence>> sequences;
      const double mTrimLeft,
         mTrimRight;
      bool committed{ false };
   };

   double mSequenceOffset { 0 };
   double mTrimLeft{ 0 };
   double mTrimRight{ 0 };

   int mRate;
   int mColourIndex;

   /*!
    @invariant `mSequences.size() > 0`
    @invariant all are non-null
    @invariant all sequences have the same lengths, append buffer lengths,
      sample formats, and sample block factory
    @invariant all cutlines have the same width
    */
   std::vector<std::unique_ptr<Sequence>> mSequences;
   //! Envelope is unique, not per-sequence
   std::unique_ptr<Envelope> mEnvelope;

   //! Cut Lines are nothing more than ordinary wave clips, with the
   //! offset relative to the start of the clip.
   /*!
    @invariant all are non-null
    */
   WaveClipHolders mCutLines {};

   // AWD, Oct. 2009: for whitespace-at-end-of-selection pasting
   bool mIsPlaceholder { false };

private:
   wxString mName;
};

#endif
