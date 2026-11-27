/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * SMbrotli: compresses and decompresses brotli files in the background.
 *
 * This is the include of the SMbrotli shim of SMArchive (sm-archive): the
 * natives of SMbrotli, run by the smarchive extension, which the shim loads.
 *
 * Each call queues a job and returns at once. The jobs run one after the
 * other in the order they were queued (by all plugins together), and each
 * job's callback is called on the main thread on a later server frame,
 * never during the call that queued it.
 *
 * The output appears only when a job succeeds: the job writes
 * "<outFile>.tmp" first and renames it onto outFile at the end. On failure
 * the temp file is deleted and outFile is left as it was.
 *
 * A plugin that unloads before its job ends gets no callback, and its job
 * is cancelled (the original SMbrotli let it run to its end). A job stops with
 * BrotliError_Write when it unpacks more than SMArchive's limits allow (the
 * sm_archive_max_* cvars). When the extension unloads, the running job is
 * aborted and queued jobs are dropped, without callbacks.
 *
 * Documentation: https://github.com/etf2l/sm-archive
 */

#if defined _brotli_included
#endinput
#endif
#define _brotli_included

/**
 * How a job ended. Every value except BrotliError_None means that outFile
 * was not written (an existing outFile is unchanged).
 *
 * The negative values are the error codes of brotli's decoder, with brotli's
 * own numbers (BROTLI_DECODER_ERROR_* in brotli's decode.h). They occur only
 * when decompressing. -23, -24, -28 and -29 are reserved by brotli and never
 * occur.
 */
enum BrotliError {
	/* The decoder could not allocate memory for a part of the stream. */
	BrotliError_AllocBlockTypeTrees = -30,
	BrotliError_AllocRingBuffer2 = -27,
	BrotliError_AllocRingBuffer1 = -26,
	BrotliError_AllocContextMap = -25,
	BrotliError_AllocTreeGroups = -22,
	BrotliError_AllocContextModes = -21,

	/* Never for SMbrotli's jobs, which use no dictionaries: listed so every
	 * code brotli defines has a name. */
	BrotliError_InvalidArguments = -20,
	BrotliError_DictionaryNotSet = -19,
	BrotliError_CompoundDictionary = -18,

	/* inFile is not a valid brotli stream: the part named is broken. */
	BrotliError_FormatBlockSwitch = -17,
	BrotliError_FormatDistance = -16,
	BrotliError_FormatPadding2 = -15,
	BrotliError_FormatPadding1 = -14,
	BrotliError_FormatWindowBits = -13, /**< Also a large-window stream, which is not supported */
	BrotliError_FormatDictionary = -12,
	BrotliError_FormatTransform = -11,
	BrotliError_FormatBlockLength2 = -10,
	BrotliError_FormatBlockLength1 = -9,
	BrotliError_FormatContextMapRepeat = -8,
	BrotliError_FormatHuffmanSpace = -7,
	BrotliError_FormatClSpace = -6,
	BrotliError_FormatSimpleHuffmanSame = -5,
	BrotliError_FormatSimpleHuffmanAlphabet = -4,
	BrotliError_FormatExuberantMetaNibble = -3,
	BrotliError_FormatReserved = -2,
	BrotliError_FormatExuberantNibble = -1,

	BrotliError_None = 0,      /**< Success, outFile is complete */
	BrotliError_OpenInput,     /**< inFile could not be opened, e.g. it does not exist */
	BrotliError_OpenOutput,    /**< "<outFile>.tmp" could not be created, e.g. its directory does not exist */
	BrotliError_OutputExists,  /**< outFile exists and overwrite was false */
	BrotliError_CreatePath,    /**< outFile's directory could not be created */
	BrotliError_Read,          /**< Reading inFile failed */
	BrotliError_Write,         /**< Writing "<outFile>.tmp" failed, e.g. the disk is full */
	BrotliError_UnexpectedEOF, /**< Decompression: inFile ends in the middle of the stream, or is empty */
	BrotliError_TrailingData,  /**< Decompression: inFile has data after the end of its stream */
	BrotliError_Memory,        /**< The encoder or decoder could not allocate its memory */
	BrotliError_Rename,        /**< "<outFile>.tmp" could not be renamed onto outFile */

	/* The decoder reached a state brotli considers impossible. */
	BrotliError_Unreachable = -31
};

/**
 * What the encoder optimizes for: brotli's BrotliEncoderMode. Every mode
 * gives a standard brotli stream; the mode only changes how well the input
 * compresses.
 */
enum BrotliMode {
	BrotliMode_Generic = 0, /**< Any data, the default */
	BrotliMode_Text,        /**< UTF-8 text, such as logs and configs */
	BrotliMode_Font         /**< WOFF 2.0 fonts */
};

/**
 * Called on the main thread when a job has ended.
 *
 * @param error      How the job ended.
 * @param inFile     inFile exactly as it was passed when the job was queued.
 * @param outFile    outFile exactly as it was passed when the job was queued.
 *                   Both are relative to the game directory, so SourceMod's
 *                   file natives (FileExists, DeleteFile, ...) take them as
 *                   they are.
 * @param data       The data passed when the job was queued.
 */
typedef BrotliCallback = function void (BrotliError error, const char[] inFile, const char[] outFile, any data);

/**
 * Paths
 *
 * inFile and outFile are relative to the game directory (for Team Fortress 2
 * the tf/ folder), with "/" between their parts. A path is refused with a
 * native error when it
 *  - is empty or starts with "/" (Windows: also "\"),
 *  - starts with "file://",
 *  - has an empty part ("a//b", a trailing "/") or a part that is "." or "..",
 *  - on Windows: contains ":".
 * On Windows, "\" separates parts as well as "/", and the check that inFile
 * and outFile are not the same file ignores case. Paths are UTF-8.
 */
methodmap Brotli {
	/**
	 * Queues the compression of a file.
	 *
	 * The job checks, in this order: outFile exists (BrotliError_OutputExists
	 * unless overwrite), outFile's directory (created if createPath, even
	 * when the job fails later), inFile can be opened.
	 *
	 * @param inFile      The file to compress.
	 * @param outFile     The brotli file to write, usually inFile plus ".br".
	 * @param callback    Called when the job has ended.
	 * @param data        Any value, passed on to the callback.
	 * @param quality     0 to 11. Higher qualities compress better and take
	 *                    much longer and more memory; 10 and 11 are many
	 *                    times slower than 9. The brotli tool's default is 11.
	 * @param window      The size of the window, the distance the encoder
	 *                    looks back for repeats, as a power of two: 10 to 24
	 *                    (1 kB to 16 MB). 0, the default, takes the smallest
	 *                    window that holds the whole file, at most 24, as the
	 *                    brotli tool does; its output at the same quality is
	 *                    then identical.
	 * @param mode        What the input is, see BrotliMode.
	 * @param overwrite   true: replace outFile if it exists. false: the job
	 *                    ends with BrotliError_OutputExists and outFile is not
	 *                    touched.
	 * @param createPath  true: create outFile's missing directories (Linux:
	 *                    mode 0775 less the server's umask). false: a missing
	 *                    directory ends the job with BrotliError_OpenOutput.
	 * @error             A path that breaks the rules above, inFile and
	 *                    outFile naming the same file, a quality outside 0 to
	 *                    11, a window that is neither 0 nor within 10 to 24,
	 *                    an invalid mode or an invalid callback. Nothing is
	 *                    queued then.
	 */
	public static native void CompressFile(const char[] inFile, const char[] outFile, BrotliCallback callback,
	                                       any data = 0, int quality = 6, int window = 0,
	                                       BrotliMode mode = BrotliMode_Generic, bool overwrite = false,
	                                       bool createPath = true);

	/**
	 * Queues the decompression of a brotli file. The file must hold exactly
	 * one brotli stream: anything after its end fails the job with
	 * BrotliError_TrailingData. Brotli has no checksum, so damaged data is
	 * only noticed when it breaks the stream's structure.
	 *
	 * The job checks outFile, its directory and inFile in the same order as
	 * CompressFile.
	 *
	 * @param inFile      The brotli file to decompress.
	 * @param outFile     The file to write.
	 * @param callback    Called when the job has ended.
	 * @param data        Any value, passed on to the callback.
	 * @param overwrite   true: replace outFile if it exists. false: the job
	 *                    ends with BrotliError_OutputExists and outFile is not
	 *                    touched.
	 * @param createPath  true: create outFile's missing directories (Linux:
	 *                    mode 0775 less the server's umask). false: a missing
	 *                    directory ends the job with BrotliError_OpenOutput.
	 * @error             A path that breaks the rules above, inFile and
	 *                    outFile naming the same file or an invalid callback.
	 *                    Nothing is queued then.
	 */
	public static native void DecompressFile(const char[] inFile, const char[] outFile, BrotliCallback callback,
	                                         any data = 0, bool overwrite = false, bool createPath = true);

	/**
	 * Writes the name of a BrotliError, e.g. "BrotliError_TrailingData",
	 * into a buffer. A name longer than the buffer is cut off; 48 bytes hold
	 * every name.
	 *
	 * @param error       The error.
	 * @param buffer      The buffer to write to.
	 * @param maxlength   The size of the buffer.
	 * @return            The number of bytes written, without the
	 *                    terminating null.
	 * @error             A value that is no BrotliError.
	 */
	public static native int ErrorString(BrotliError error, char[] buffer, int maxlength);
}

/**
 * Do not edit below this line!
 */
public Extension __ext_brotli = {
	name = "SMbrotli",
	file = "smbrotli.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_brotli_SetNTVOptional() {
	MarkNativeAsOptional("Brotli.CompressFile");
	MarkNativeAsOptional("Brotli.DecompressFile");
	MarkNativeAsOptional("Brotli.ErrorString");
}
#endif
