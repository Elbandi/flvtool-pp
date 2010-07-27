/*
 * flvtool++
 *
 * Copyright (c) 2007-2009 by Dan Weatherford and Facebook, inc.
 * All rights reserved.
 */

#include "common.h"
#include "AMFData.h"
#include "mmfile.h"
#include "fout.h"
#include "serialized_buffer.h"
#include "bitstream.h"

inline uint32_t deserialize_uint24(char*& ptr) {
  uint32_t d = ((*(ptr++)) & 0xff) << 16;
  d += ((*(ptr++)) & 0xff) << 8;
  d += ((*(ptr++)) & 0xff);
  return d;
}

uint32_t process_timestamp(char tag_type, char*& fptr, uint32_t& last_timestamp) {
  static bool timestamp_warning_given = false;

  uint32_t tag_timestamp = deserialize_uint24(fptr);
  tag_timestamp |= ((*(fptr++)) & 0xff) << 24; // add upper 8 bits of the timestamp field from TimestampExtended

  if (tag_timestamp < last_timestamp) {
    if (((tag_timestamp & 0xff000000) == 0) && (last_timestamp & 0xfff00000)) {
      // Looks like the file doesn't have the TimestampExtended field properly set.
      if (! timestamp_warning_given) {
        timestamp_warning_given = true;
        printf("WARNING: Fixing wrapped timestamps produced by an encoder that doesn't understand TimestampExtended\n");
      }
      uint32_t new_timestamp = tag_timestamp + (last_timestamp & 0xff000000);
      if (new_timestamp < last_timestamp) new_timestamp += 0x1000000;
      tag_timestamp = new_timestamp;
      assert(tag_timestamp >= last_timestamp);
    } else {
      if (! timestamp_warning_given && (tag_type == 9 || tag_type == 18)) { // don't warn on tags that aren't video or audio...
        printf("WARNING: File has discontiguous timestamps that we don't know how to fix.\n");
        timestamp_warning_given = true;
      }
    }
  }

  if (tag_type == 9) { // only track last timestamp for video frames
    last_timestamp = std::max(tag_timestamp, last_timestamp);
  }
  return tag_timestamp;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("flvtool++ 1.2.1\nCopyright (c) 2007-2009 Dan Weatherford and Facebook, inc.\n");
    printf("http://developers.facebook.com/opensource.php\n");
    printf("Published under the BSD license.\n\n");
    printf("usage: flvtool++ [options] [input filename] [output filename]\n");
    printf("  -nodump: do not dump the metadata when done (kinda quiet)\n");
    printf("  -nomerge: do not merge existing data from the onMetaData tag (if present) in the input file\n");
    printf("  -nometapackets: do not copy extra metadata packets from the input file (besides the initial onMetaData packet)\n");
    printf("  -strip: do not emit any metadata to the output file; implies -nometapackets\n");
    printf("  -tag name value: Set a metadata tag named 'name' to the (string) value 'value'\n");
    printf("Note that manually set tags will override automatically generated tags.\n");
    return -1;
  }

  char* filename = NULL;
  char* outFilename = NULL;
  string outFilename_tmp;
  bool nomerge = false;
  bool nodump = false;
  bool nometapackets = false;
  bool strip = false;
  list<pair<string, string> > extra_tags;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-nomerge") == 0) {
      nomerge = true;
    }
    else if (strcmp(argv[i], "-nodump") == 0) {
      nodump = true;
    }
    else if (strcmp(argv[i], "-nometapackets") == 0) {
      nometapackets = true;
    }
    else if (strcmp(argv[i], "-strip") == 0) {
      strip = true;
      nometapackets = true;
    }
    else if (strcmp(argv[i], "-tag") == 0) {
      string tn = argv[++i];
      string tv = argv[++i];
      extra_tags.push_back(std::make_pair(tn, tv));
    }
    else if (! filename) {
      filename = argv[i];
    }
    else {
      outFilename = argv[i];
    }  
  }

  if (! filename) {
    printf("Need a filename, chief\n");
    return -1;
  }
  if (outFilename) {
    outFilename_tmp = string(outFilename) + ".tmp";
  }
  else {
    printf("No output filename -- not hinting, showing existing metadata only\n");
  }

  try {
    mmfile infile(filename);

    if (infile.flen < 13) {
      printf("Input file is not long enough to contain a valid FLV header (need 13 bytes, got %lu)\n", infile.flen);
      exit(-1);
    }

    char* fptr = infile.fbase;
    char* fend = fptr + infile.flen;
    
    // check magic & version
    if (! ((*(fptr++) == 'F') &&
           (*(fptr++) == 'L') &&
           (*(fptr++) == 'V') &&
           (*(fptr++) == 0x01)) ) {
    
      printf("bailing on invalid magic or version\n");
      exit(-1);
    }
    shared_ptr<AMFMixedArray> onMetaData(new AMFMixedArray());

    // ignore flags byte
    ++fptr;
    //char flags = *(fptr++);
    bool hasVideo = false; //(flags & 0x04);
    bool hasAudio = false; //(flags & 0x01);

    // grab header size
    uint32_t header_size = ntohl(*reinterpret_cast<uint32_t*>(fptr));
    fptr += 4;
  
    // we don't care about the extra data, just skip it
    fptr += (header_size - 9);
    fptr += 4; // skip the uint32_t unknown extra (should be 0)

    char* tag_stream_start = fptr; // save this ptr

    size_t total_audio = 0, total_video = 0;
    uint32_t last_timestamp = 0;
    bool have_audio_params = false, have_video_params = false;
    bool hasKeyframes = false;
    uint32_t vframe_count = 0; // total video frames
    uint32_t keyframe_count = 0; // keyframe count only

    while (fptr < fend) {
      char* tag_start = fptr;
      if ((tag_start + 15) > fend) { // If we don't have at least 15 bytes worth of data, this isn't a complete tag.
        printf("WARNING: extra junk at end of file (%zu bytes' worth)\n", (size_t)(fend - fptr));
        // Adjust file end ptr to end of previous tag for the tag copying process
        fend = (tag_start - 1);
        break;
      }
      char tag_type = *(fptr++);
      uint32_t tag_length = deserialize_uint24(fptr);
      if ((tag_start + tag_length) > fend) {
        printf("WARNING: Tag of type %u (%u bytes) at 0x%zx extends past the end of the file; will truncate the stream here.\n", tag_type, tag_length, tag_start - infile.fbase);
        fend = tag_start;
        break;
      }
      uint32_t tag_timestamp = process_timestamp(tag_type, fptr, last_timestamp);
      fptr += 3; // skip uint24_t stream ID (should be 0)


      if (tag_type == 18) { // meta
        serialized_buffer tagbuf(fptr, tag_length);

        try {
          shared_ptr<AMFData> tagKey = AMFData::construct(tagbuf);
          shared_ptr<AMFData> d = AMFData::construct(tagbuf);

          if (tagKey->asString() == "onMetaData") {
            if (! nomerge) {
              printf("Merging existing onMetaData tag\n");
              onMetaData->merge(d, false);
            }
          }
          else {
            printf("META tag (key %s):\n%s\n", tagKey->asString().c_str(), d->asString().c_str());
          }
        } catch (const std::exception& e) {
          printf("Error reading metadata tag: %s\n", e.what());
        }
        fptr += tag_length;
      }
      else if (tag_type == 9) { // video
        hasVideo = true;
        // Frame types: 1 = Keyframe, 2 = IFrame, 3 = Disposable IFrame
        char codec_id_and_frame_type = *(fptr++);
        char codec_id = (codec_id_and_frame_type & 0x0f);
        char frame_type = (codec_id_and_frame_type >> 4) & 0x0f;
        if (frame_type == 1) { // Keyframe
          hasKeyframes = true;
          ++keyframe_count;
        }
        if (! have_video_params) {
          const char* codec;
          int w = 0, h = 0;
          switch (codec_id) {
            case 2: codec = "H.263"; break;
            case 3: codec = "SCREEN"; break;
            case 4: codec = "VP6"; break;
            case 6: codec = "SCREEN v2"; break;
            case 7: codec = "H.264"; break;
            default: codec = "(unknown)";
           };
          // Scrape width & height data from the video
          char* vptr = fptr;
          switch (codec_id) {
            case 2: { // H.263
              vptr += 3;
              // yes, these flags and bytes span byte boundaries by ONE BIT (bastards)
              char dim_flag = (((*vptr) & 0x03) << 1) + (((vptr[1]) & 0x80) >> 7);
              ++vptr;
              switch (dim_flag) {
                case 0: // abs w/h encoded as uint8s
                  w = ((vptr[0] & 0x7f) << 1) + ((vptr[1] & 0x80) >> 7);
                  h = ((vptr[1] & 0x7f) << 1) + ((vptr[2] & 0x80) >> 7);
                  break;
                case 1: // abs w/h encoded as uint16s (BE)
                  w  = ((vptr[0] & 0x7f) << 1) + ((vptr[1] & 0x80) >> 7) << 8;
                  w += ((vptr[1] & 0x7f) << 1) + ((vptr[2] & 0x80) >> 7);
                  h  = ((vptr[2] & 0x7f) << 1) + ((vptr[3] & 0x80) >> 7) << 8;
                  h += ((vptr[3] & 0x7f) << 1) + ((vptr[4] & 0x80) >> 7);
                  break;
                case 2: w=352; h=288; break;
                case 3: w=176; h=144; break;
                case 4: w=128; h=96; break;
                case 5: w=320; h=240; break;
                case 6: w=160; h=120; break;
              };
              } break;
            case 3: // SCREEN
                // W & H encoded as 12-bit uints starting from halfway through the first byte
                w  = ((*(vptr++)) & 0x0f) << 8;
                w += ((*(vptr++)) & 0xff);
                h  = ((*(vptr++)) & 0xff) << 4;
                h  = ((*(vptr++)) & 0xf0) >> 4;
              break;
            case 4: // VP6.2
                // [4] and [5] are the number of displayed macroblock rows/cols (respectively). Macrolocks are 16 px wide.
                w = (vptr[4] & 0xff) * 16;
                h = (vptr[5] & 0xff) * 16;
                // and [0] is two adjustment values subtracted from w (high 4) and h (low 4)
                h -= (vptr[0] & 0x0f);
                w -= ((vptr[0] & 0xf0) >> 4);
              break;
            case 7: { // H.264
              uint8_t avc_packet_type = *(vptr++);
              vptr += 3; // skip the composition time (SI24)
              if (avc_packet_type == 0) {
                // skip 8 bytes worth of isom avcC data in the sequence header before trying to decode a NALu
                vptr += 8;
              }
              else if (avc_packet_type != 1) goto didnt_get_video_params; // want an AVC NAL unit
              //printf("Trying to decode h.264 NAL unit at file offset 0x%zx\n", vptr - infile.fbase);
              serialized_buffer avc_buffer(vptr, tag_length - 4);
              bitstream avc(&avc_buffer);

              if (avc.get_bit()) {
                printf("AVC NAL header decode: forbidden_zero_bit is 1?\n");
                goto didnt_get_video_params;
              }
              avc.get_bits(2); // nal_ref_idc
              uint8_t nal_unit_type = avc.get_bits(5);
              if (nal_unit_type != 7) goto didnt_get_video_params; // need seq_parameter_set_rbsp

              uint8_t profile_idc = avc.get_bits(8);
              avc.get_bits(8); // skip constraint_set[0-3]_flag, reserved_zero_4bits
              avc.get_bits(8); // level_idc

              avc.get_golomb_ue();// seq_parameter_set_id

              if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144) {
                uint32_t chroma_format_idc = avc.get_golomb_ue();
                if (chroma_format_idc == 3) avc.get_bit(); // residual_colour_transform_flag
                avc.get_golomb_ue(); // bit_depth_luma_minus8
                avc.get_golomb_ue(); // bit_depth_chroma_minus8
                avc.get_bits(1); // qpprime_y_zero_transform_bypass_flag
                bool seq_scaling_matrix_present = avc.get_bits(1);
                if (seq_scaling_matrix_present) {
                  // TODO
                  printf("AVC seq_parameter_set_rbsp decode: UNHANDLED: seq_scaling_matrix_present = 1\n");
                  break;
                }
              }
              avc.get_golomb_ue(); // log2_max_frame_num_minus4
              uint32_t pic_order_cnt_type = avc.get_golomb_ue();

              if (pic_order_cnt_type == 0) {
                avc.get_golomb_ue(); // log2_max_pic_order_cnt_lsb_minus4
              } else if (pic_order_cnt_type == 1) {
                avc.get_bit(); // delta_pic_order_always_zero_flag
                avc.get_golomb_se(); // offset_for_non_ref_pic
                avc.get_golomb_se(); // offset_for_top_to_bottom_field
                uint32_t num_ref_frames_in_pic_order_cnt_cycle = avc.get_golomb_ue();
                for (uint32_t frame_idx = 0; frame_idx < num_ref_frames_in_pic_order_cnt_cycle; ++frame_idx) {
                  avc.get_golomb_se();
                }
              }
              avc.get_golomb_ue(); // num_ref_frames
              avc.get_bit(); // gaps_in_frame_num_value_allowed_flag

              uint32_t pic_width_in_mbs = avc.get_golomb_ue() + 1;
              uint32_t pic_height_in_map_units = avc.get_golomb_ue() + 1;

              bool frames_mbs_only = avc.get_bit();
              if (! frames_mbs_only) avc.get_bit(); // mb_adaptive_frame_field

              avc.get_bit(); // direct_8x8_inference_flag

              uint32_t left_offset = 0, right_offset = 0, top_offset = 0, bottom_offset = 0;
              bool frame_cropping = avc.get_bit();

              if (frame_cropping) {
                left_offset = avc.get_golomb_ue() * 2;
                right_offset = avc.get_golomb_ue() * 2;
                top_offset = avc.get_golomb_ue() * 2;
                bottom_offset = avc.get_golomb_ue() * 2;
                if (! frames_mbs_only) {
                  // interlaced source multiplies the top/bottom crop offsets by 2
                  top_offset *= 2;
                  bottom_offset *= 2;
                }
              }
              w = pic_width_in_mbs * 16 - (left_offset + right_offset);
              h = pic_height_in_map_units * 16 - (top_offset + bottom_offset);
              if (! frames_mbs_only) {
                h *= 2; // map units are twice as big as macroblocks for interlaced sources.
              }

              } break;
          }
          onMetaData->dmap["videocodecid"] = shared_ptr<AMFData>(new AMFDouble(codec_id));
          // decode width & height based on video stream type
          have_video_params = true;
          printf("Video: %dx%d %s\n", w, h, codec);
          if (w) onMetaData->dmap["width"] = shared_ptr<AMFData>(new AMFDouble(w));
          if (h) onMetaData->dmap["height"] = shared_ptr<AMFData>(new AMFDouble(h));
        }
didnt_get_video_params:
        //printf("Video frame: length 0x%x bytes. Codec: %s. Type: %s.\n", tag_length - 1, codec, frame);
        fptr += (tag_length - 1); // (we already ate the codec_id_and_tag_type byte)
        total_video += (tag_length - 1); // accumulate video byte count, minus the codec_id_and_tag_type byte
        ++vframe_count;
      }

      /*
        Adobe FMS' API method Stream.record(...) sometimes generates
        zero size audio tags at arbitrary position.
      */
      else if (tag_type == 8 && tag_length > 0) {
        hasAudio = true;
        char audio_format_byte = *(fptr++);
        char audio_format = ((audio_format_byte >> 4) & 0x0f); 
        int audio_rate = 0;
        switch ((audio_format_byte >> 2) & 0x03) {
          case 0: audio_rate =  5500; break;
          case 1: audio_rate = 11000; break;
          case 2: audio_rate = 22000; break;
          case 3: audio_rate = 44100; break;
        };
        int audio_sample_size = (audio_format_byte & 0x02) ? 16 : 8; 
        bool stereo = (audio_format_byte & 0x01);
        if (audio_format == 4) {
          // Special case for 16kHz Mono NellyMoser audio
          audio_sample_size = 8;
          audio_rate = 16000;
          stereo = false;
        } else if (audio_format == 5) {
          // 8kHz Mono NellyMoser audio
          audio_sample_size = 8;
          audio_rate = 8000;
          stereo = false;
        }
        if (! have_audio_params) {
          onMetaData->dmap["audiocodecid"] = shared_ptr<AMFData>(new AMFDouble(audio_format));
          onMetaData->dmap["audiosamplerate"] = shared_ptr<AMFData>(new AMFDouble(audio_rate));
          onMetaData->dmap["audiosamplesize"] = shared_ptr<AMFData>(new AMFDouble(audio_sample_size));
          onMetaData->dmap["stereo"] = shared_ptr<AMFData>(new AMFBoolean(stereo));
          const char* audio_format_str = NULL;
          switch (audio_format) {
            case 0: audio_format_str = "Uncompressed"; break;
            case 1: audio_format_str = "ADPCM"; break;
            case 2: audio_format_str = "MP3"; break;
            case 3: audio_format_str = "Linear PCM (little endian)"; break;
            case 4: audio_format_str = "NellyMoser (16kHz Mono special case)"; break;
            case 5: audio_format_str = "NellyMoser (8kHz Mono special case)"; break;
            case 6: audio_format_str = "NellyMoser"; break;
            case 7: audio_format_str = "G.711 A-law log PCM"; break;
            case 8: audio_format_str = "G.711 mu-law log PCM"; break;
            case 10: audio_format_str = "AAC"; break;
            case 11: audio_format_str = "Speex"; break;
            case 14: audio_format_str = "MP3 8 kHz"; break;
          }
          printf("Audio: %dHz %dbit %s, codec ID %d (%s)\n", audio_rate, audio_sample_size, (stereo ? "stereo" : "mono"), audio_format, audio_format_str);
          have_audio_params = true;
        }
 
        fptr += (tag_length - 1); // skip rest of audio except for the format byte that we ate
        total_audio += (tag_length); // accumulate audio byte count
      }
      else {
        if (tag_length > 0) {
          printf("WARNING: Skipping unknown tag type %u (%u bytes, timestamp %u ms) at file offset 0x%zx\n", tag_type & 0xff, tag_length, tag_timestamp, (size_t)(tag_start - infile.fbase));
        } else {
          printf("INFO: Skipping zero size audio tag at file offset 0x%zx\n", (size_t)(tag_start - infile.fbase));
        }
        fptr += tag_length;
      } 
      fptr += 4; // skip length postfix
    }
    double length_sec = (double)last_timestamp / 1000.0;
    double videodatarate = (((double)total_video * 8.0) / 1000.0) / length_sec;
    double audiodatarate = (((double)total_audio * 8.0) / 1000.0) / length_sec;
    double framerate = (double)(vframe_count)/length_sec;
    onMetaData->dmap["hasAudio"] = shared_ptr<AMFData>(new AMFBoolean(hasAudio));
    onMetaData->dmap["hasVideo"] = shared_ptr<AMFData>(new AMFBoolean(hasVideo));
    onMetaData->dmap["hasCuePoints"] = shared_ptr<AMFData>(new AMFBoolean(false));
    onMetaData->dmap["hasMetadata"] = shared_ptr<AMFData>(new AMFBoolean(true));
    onMetaData->dmap["canSeekToEnd"] = shared_ptr<AMFData>(new AMFBoolean(true));
    onMetaData->dmap["duration"] = shared_ptr<AMFData>(new AMFDouble(length_sec));
    onMetaData->dmap["framerate"] = shared_ptr<AMFData>(new AMFDouble(framerate));
    onMetaData->dmap["videodatarate"] = shared_ptr<AMFData>(new AMFDouble(videodatarate));
    onMetaData->dmap["audiodatarate"] = shared_ptr<AMFData>(new AMFDouble(audiodatarate));
    onMetaData->dmap["videosize"] = shared_ptr<AMFData>(new AMFDouble(total_video));
    onMetaData->dmap["audiosize"] = shared_ptr<AMFData>(new AMFDouble(total_audio));
    onMetaData->dmap["hasKeyframes"] = shared_ptr<AMFData>(new AMFBoolean(hasKeyframes));
    onMetaData->dmap["totalframes"] = shared_ptr<AMFData>(new AMFDouble(vframe_count));
    onMetaData->dmap["lasttimestamp"] = shared_ptr<AMFData>(new AMFDouble((double)last_timestamp / 1000.0));
    onMetaData->dmap["datasize"] = shared_ptr<AMFData>(new AMFDouble(0)); // backpatch this

    if (! outFilename) {
      // dump only mode
      puts(onMetaData->asString().c_str());
      return 0;
    }

    onMetaData->dmap["metadatacreator"] = shared_ptr<AMFData>(new AMFString("flvtool++ (Facebook, Motion project, dweatherford)"));
    onMetaData->dmap["metadatadate"] = shared_ptr<AMFData>(new AMFDate());

    for (list<pair<string, string> >::const_iterator eti = extra_tags.begin(); eti != extra_tags.end(); ++eti) {
      onMetaData->dmap[eti->first] = shared_ptr<AMFData>(new AMFString(eti->second));
    }

    // Allocate some storage for the keyframe indices we'll build
    shared_ptr<AMFArray> keyTimes(new AMFArray());
    shared_ptr<AMFArray> keyPositions(new AMFArray());
    
    shared_ptr<AMFObject> keyframes(new AMFObject());
    keyframes->dmap["times"] = keyTimes;
    keyframes->dmap["filepositions"] = keyPositions;
    onMetaData->dmap["keyframes"] = keyframes;
    // Resize the arrays to the final size so we can calculate the metadata length (and thus the file positions of the key tags)
    for (uint32_t s = 0; s < keyframe_count; ++s) {
      keyTimes->dmap.push_back(shared_ptr<AMFData>(new AMFDouble(0.0)));
      keyPositions->dmap.push_back(shared_ptr<AMFData>(new AMFDouble(0.0)));
    }

    // If we're stripping the metadata then clear the onMetaData block
    // It throws away some work earlier, but oh well, it was easy
    if (strip) onMetaData->dmap.clear();

    // Open the output file
    // write to temporary file then rename into place
    // in case the output and input files are the same file
    fout fp(outFilename_tmp.c_str());
    // Write the standard header (using our previously obtained flags byte)
    fp.write("FLV\x01", 4);
    // build flags
    uint8_t flags = 0;
    if (hasVideo) flags |= 0x04;
    if (hasAudio) flags |= 0x01;
    fp.putc(flags);
    fp.write("\x00\x00\x00\x09\x00\x00\x00\x00", 8);
    // Write the onMetaData tag
    // save the location of the actual 
    fp.putc(18); // meta tag start
    size_t fp_metadata_length_offset = fp.tell();
    fp.write("\x00\x00\x00", 3); // NULL out the length -- we backpatch later
    fp.write("\x00\x00\x00\x00", 4); // Timestamp + TimestampExtended = 0
    fp.write("\x00\x00\x00", 3); // uint24 stream ID = 0
    AMFString mthead("onMetaData");
    size_t fp_metadata_real_start = fp.tell(); // and this one when calculating the length
    mthead.write(fp);
    size_t fp_metadata_start = fp.tell(); // use this one when backpatching over the metadata
    onMetaData->write(fp);
    size_t fp_metadata_len = fp.tell() - fp_metadata_real_start;
    // write tag_size uint32 (incl. header size)
    fp.write<uint32_t>(htons(fp_metadata_len + 11));
    size_t fp_tagstream_start = fp.tell();
    // backpatch metadata tag's length -- uint24
    fp.seek(fp_metadata_length_offset);
    fp.putc((fp_metadata_len >> 16) & 0xff);
    fp.putc((fp_metadata_len >> 8) & 0xff);
    fp.putc( fp_metadata_len & 0xff);
    fp.seek(fp_tagstream_start);
    
    // Copy tags from input to output file, making note of keyframe tag positions and timestamps
    fptr = tag_stream_start;
    uint32_t current_keyframe = 0;
    last_timestamp = 0; // reset for fixing missing timestampextended field
    while (fptr < fend) {
      const char* tag_start = fptr;
      char tag_type = *(fptr++);
      uint32_t tag_length = deserialize_uint24(fptr);
      uint32_t tag_timestamp = process_timestamp(tag_type, fptr, last_timestamp);
      uint32_t streamID = deserialize_uint24(fptr);

      if (tag_type == 9) { // video
        // Frame types: 1 = Keyframe, 2 = IFrame, 3 = Disposable IFrame
        char codec_id_and_frame_type = *fptr;
        char frame_type = (codec_id_and_frame_type >> 4) & 0x0f;
        if (frame_type == 1) { // Keyframe
          keyTimes->dmap[current_keyframe] = shared_ptr<AMFData>(new AMFDouble((double)tag_timestamp / 1000.0));
          keyPositions->dmap[current_keyframe] = shared_ptr<AMFData>(new AMFDouble(fp.tell()));
          ++current_keyframe;
        }
      }

    if ((tag_type == 8 && tag_length > 0) || tag_type == 9 || (tag_type == 18 && (!nometapackets))) {
        // Write AUDIO/VIDEO/META tag header
        fp.putc(tag_type); // type
        fp.write_u24_be(tag_length); // length
        fp.write_u24_be(tag_timestamp); // timestamp
        fp.putc((tag_timestamp >> 24) & 0xff); //timestampextended
        fp.write_u24_be(streamID); // streamID
        // Copy tag body
        fp.write(fptr, tag_length + 4);
      } else {
        if ((fptr + tag_length + 4) > fend) {
          printf("SEVERE: Unknown tag at 0x%zx of %u bytes extends past the end of the file; stopping tag copy here.\n", (size_t)(tag_start - infile.fbase), tag_length);
        } else if (tag_length > 0) {
          printf("WARNING: Skipping unknown tag type %u (%u bytes, timestamp %u ms) at file offset 0x%zx\n", tag_type & 0xff, tag_length, tag_timestamp, (size_t)(tag_start - infile.fbase));
        } else {
          printf("INFO: Skipping zero size audio tag at file offset 0x%zx\n", (size_t)(tag_start - infile.fbase));
        }
      }

      fptr += (tag_length + 4); // move pointer to top of next tag
    }
    // Done copying tags, regenerate & backpatch updated metadata
    // update file length
    if (!strip) onMetaData->dmap["datasize"] = shared_ptr<AMFData>(new AMFDouble(fp.tell())); 
    fp.seek(fp_metadata_start);
    onMetaData->write(fp);
   
    // done with our mmfile
    // close first in case the output is going to overwrite this on rename
    infile.close();
 
    // close & rename into place
    fp.close();
    rename(outFilename_tmp.c_str(), outFilename);

    printf("Total: %lu video bytes (%f kbps), %lu audio bytes (%f kbps), %f seconds long\n", total_video, videodatarate, total_audio, audiodatarate, length_sec);
    if (! nodump) {
      printf("Final onMetaData tag contents: %s\n", onMetaData->asString().c_str());
    }

  } catch (const std::exception& e) {
    printf("xcpt: %s\n", e.what());
    exit(-1);
  }
  return 0;
}

