/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * SMbz2: compresses and decompresses bzip2 files in the background.
 *
 * This is the include of the SMbz2 shim of SMArchive (sm-archive): the
 * natives of SMbz2, run by the smarchive extension, which the shim loads.
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
 * is cancelled (the original SMbz2 let it run to its end). A job stops with
 * BZ2Error_Write when it unpacks more than SMArchive's limits allow (the
 * sm_archive_max_* cvars). When the extension unloads, the running job is
 * aborted and queued jobs are dropped, without callbacks.
 *
 * Documentation: https://github.com/etf2l/sm-archive
 */

#if defined _bzip2_included
#endinput
#endif
#define _bzip2_included

/**
 * How a job ended. Every value except BZ2Error_None means that outFile was
 * not written (an existing outFile is unchanged).
 */
enum BZ2Error {
	BZ2Error_None = 0,      /**< Success, outFile is complete */
	BZ2Error_OpenInput,     /**< inFile could not be opened, e.g. it does not exist */
	BZ2Error_OpenOutput,    /**< "<outFile>.tmp" could not be created, e.g. its directory does not exist */
	BZ2Error_OutputExists,  /**< outFile exists and overwrite was false */
	BZ2Error_CreatePath,    /**< outFile's directory could not be created */
	BZ2Error_Read,          /**< Reading inFile failed */
	BZ2Error_Write,         /**< Writing "<outFile>.tmp" failed, e.g. the disk is full */
	BZ2Error_Data,          /**< Decompression: the compressed data is corrupt */
	BZ2Error_DataMagic,     /**< Decompression: inFile, or data after its last bzip2 stream, is not bzip2 */
	BZ2Error_UnexpectedEOF, /**< Decompression: inFile ends in the middle of a bzip2 stream, or is empty */
	BZ2Error_Memory,        /**< libbzip2 ran out of memory */
	BZ2Error_Rename         /**< "<outFile>.tmp" could not be renamed onto outFile */
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
typedef BZ2Callback = function void (BZ2Error error, const char[] inFile, const char[] outFile, any data);

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
methodmap BZ2 {
	/**
	 * Queues the compression of a file.
	 *
	 * The job checks, in this order: outFile exists (BZ2Error_OutputExists
	 * unless overwrite), outFile's directory (created if createPath, even
	 * when the job fails later), inFile can be opened.
	 *
	 * @param inFile      The file to compress.
	 * @param outFile     The bzip2 file to write, usually inFile plus ".bz2".
	 * @param callback    Called when the job has ended.
	 * @param data        Any value, passed on to the callback.
	 * @param level       1 to 9: the block size in units of 100 kB. Higher
	 *                    levels usually compress better and need more
	 *                    memory. 9 is the default of the bzip2 tool, whose
	 *                    output at the same level is identical.
	 * @param overwrite   true: replace outFile if it exists. false: the job
	 *                    ends with BZ2Error_OutputExists and outFile is not
	 *                    touched.
	 * @param createPath  true: create outFile's missing directories (Linux:
	 *                    mode 0775 less the server's umask). false: a missing
	 *                    directory ends the job with BZ2Error_OpenOutput.
	 * @error             A path that breaks the rules above, inFile and
	 *                    outFile naming the same file, a level outside 1 to 9
	 *                    or an invalid callback. Nothing is queued then.
	 */
	public static native void CompressFile(const char[] inFile, const char[] outFile, BZ2Callback callback,
	                                       any data = 0, int level = 9, bool overwrite = false,
	                                       bool createPath = true);

	/**
	 * Queues the decompression of a bzip2 file. A file of several bzip2
	 * streams one after the other (written by pbzip2, or .bz2 files joined
	 * with cat) is decompressed completely.
	 *
	 * The job checks outFile, its directory and inFile in the same order as
	 * CompressFile.
	 *
	 * @param inFile      The bzip2 file to decompress.
	 * @param outFile     The file to write.
	 * @param callback    Called when the job has ended.
	 * @param data        Any value, passed on to the callback.
	 * @param overwrite   true: replace outFile if it exists. false: the job
	 *                    ends with BZ2Error_OutputExists and outFile is not
	 *                    touched.
	 * @param createPath  true: create outFile's missing directories (Linux:
	 *                    mode 0775 less the server's umask). false: a missing
	 *                    directory ends the job with BZ2Error_OpenOutput.
	 * @error             A path that breaks the rules above, inFile and
	 *                    outFile naming the same file or an invalid callback.
	 *                    Nothing is queued then.
	 */
	public static native void DecompressFile(const char[] inFile, const char[] outFile, BZ2Callback callback,
	                                         any data = 0, bool overwrite = false, bool createPath = true);

	/**
	 * Writes the name of a BZ2Error, e.g. "BZ2Error_DataMagic", into a
	 * buffer. A name longer than the buffer is cut off.
	 *
	 * @param error       The error.
	 * @param buffer      The buffer to write to.
	 * @param maxlength   The size of the buffer.
	 * @return            The number of bytes written, without the
	 *                    terminating null.
	 * @error             A value that is no BZ2Error.
	 */
	public static native int ErrorString(BZ2Error error, char[] buffer, int maxlength);
}

/**
 * Do not edit below this line!
 */
public Extension __ext_bzip2 = {
	name = "SMbz2",
	file = "smbz2.ext",
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
public void __ext_bzip2_SetNTVOptional() {
	MarkNativeAsOptional("BZ2.CompressFile");
	MarkNativeAsOptional("BZ2.DecompressFile");
	MarkNativeAsOptional("BZ2.ErrorString");
}
#endif
