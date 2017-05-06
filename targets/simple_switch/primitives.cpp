/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include <bm/bm_sim/actions.h>
#include <bm/bm_sim/calculations.h>
#include <bm/bm_sim/core/primitives.h>
#include <bm/bm_sim/counters.h>
#include <bm/bm_sim/meters.h>
#include <bm/bm_sim/packet.h>
#include <bm/bm_sim/phv.h>
#include <rohc/rohc_compressor_module.h>
#include <rohc/rohc_decompressor_module.h>

#include <random>
#include <thread>

template <typename... Args>
using ActionPrimitive = bm::ActionPrimitive<Args...>;

using bm::Data;
using bm::Field;
using bm::Header;
using bm::MeterArray;
using bm::CounterArray;
using bm::RegisterArray;
using bm::NamedCalculation;
using bm::HeaderStack;
using bm::PHV;

using ROHC::RohcCompressorEntity;
using ROHC::RohcDecompressorEntity;

RohcDecompressorEntity rohc_d_ent(true, false);
RohcCompressorEntity rohc_c_ent(true, false);

class modify_field : public ActionPrimitive<Data &, const Data &> {
  void operator ()(Data &dst, const Data &src) {
    bm::core::assign()(dst, src);
  }
};

REGISTER_PRIMITIVE(modify_field);

class modify_field_rng_uniform
  : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &b, const Data &e) {
    // TODO(antonin): a little hacky, fix later if there is a need using GMP
    // random fns
    using engine = std::default_random_engine;
    using hash = std::hash<std::thread::id>;
    static thread_local engine generator(hash()(std::this_thread::get_id()));
    using distrib64 = std::uniform_int_distribution<uint64_t>;
    distrib64 distribution(b.get_uint64(), e.get_uint64());
    f.set(distribution(generator));
  }
};

REGISTER_PRIMITIVE(modify_field_rng_uniform);

class add_to_field : public ActionPrimitive<Field &, const Data &> {
  void operator ()(Field &f, const Data &d) {
    f.add(f, d);
  }
};

REGISTER_PRIMITIVE(add_to_field);

class subtract_from_field : public ActionPrimitive<Field &, const Data &> {
  void operator ()(Field &f, const Data &d) {
    f.sub(f, d);
  }
};

REGISTER_PRIMITIVE(subtract_from_field);

class add : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.add(d1, d2);
  }
};

REGISTER_PRIMITIVE(add);

class subtract : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.sub(d1, d2);
  }
};

REGISTER_PRIMITIVE(subtract);

class bit_xor : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.bit_xor(d1, d2);
  }
};

REGISTER_PRIMITIVE(bit_xor);

class bit_or : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.bit_or(d1, d2);
  }
};

REGISTER_PRIMITIVE(bit_or);

class bit_and : public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.bit_and(d1, d2);
  }
};

REGISTER_PRIMITIVE(bit_and);

class shift_left :
  public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.shift_left(d1, d2);
  }
};

REGISTER_PRIMITIVE(shift_left);

class shift_right :
  public ActionPrimitive<Data &, const Data &, const Data &> {
  void operator ()(Data &f, const Data &d1, const Data &d2) {
    f.shift_right(d1, d2);
  }
};

REGISTER_PRIMITIVE(shift_right);

class drop : public ActionPrimitive<> {
  void operator ()() {
    get_field("standard_metadata.egress_spec").set(511);
    if (get_phv().has_field("intrinsic_metadata.mcast_grp")) {
      get_field("intrinsic_metadata.mcast_grp").set(0);
    }
  }
};

REGISTER_PRIMITIVE(drop);

class exit_ : public ActionPrimitive<> {
  void operator ()() {
    get_packet().mark_for_exit();
  }
};

REGISTER_PRIMITIVE_W_NAME("exit", exit_);

class generate_digest : public ActionPrimitive<const Data &, const Data &> {
  void operator ()(const Data &receiver, const Data &learn_id) {
    // discared receiver for now
    (void) receiver;
    get_field("intrinsic_metadata.lf_field_list").set(learn_id);
  }
};

REGISTER_PRIMITIVE(generate_digest);

class add_header : public ActionPrimitive<Header &> {
  void operator ()(Header &hdr) {
    // TODO(antonin): reset header to 0?
    if (!hdr.is_valid()) {
      hdr.reset();
      hdr.mark_valid();
      // updated the length packet register (register 0)
      auto &packet = get_packet();
      packet.set_register(0, packet.get_register(0) + hdr.get_nbytes_packet());
    }
  }
};

REGISTER_PRIMITIVE(add_header);

class add_header_fast : public ActionPrimitive<Header &> {
  void operator ()(Header &hdr) {
    hdr.mark_valid();
  }
};

REGISTER_PRIMITIVE(add_header_fast);

class remove_header : public ActionPrimitive<Header &> {
  void operator ()(Header &hdr) {
    if (hdr.is_valid()) {
      // updated the length packet register (register 0)
      auto &packet = get_packet();
      packet.set_register(0, packet.get_register(0) - hdr.get_nbytes_packet());
      hdr.mark_invalid();
    }
  }
};

REGISTER_PRIMITIVE(remove_header);

class copy_header : public ActionPrimitive<Header &, const Header &> {
  void operator ()(Header &dst, const Header &src) {
    bm::core::assign_header()(dst, src);
  }
};

REGISTER_PRIMITIVE(copy_header);

/* standard_metadata.clone_spec will contain the mirror id (16 LSB) and the
   field list id to copy (16 MSB) */
class clone_ingress_pkt_to_egress
  : public ActionPrimitive<const Data &, const Data &> {
  void operator ()(const Data &clone_spec, const Data &field_list_id) {
    Field &f_clone_spec = get_field("standard_metadata.clone_spec");
    f_clone_spec.shift_left(field_list_id, 16);
    f_clone_spec.add(f_clone_spec, clone_spec);
  }
};

REGISTER_PRIMITIVE(clone_ingress_pkt_to_egress);

class clone_egress_pkt_to_egress
  : public ActionPrimitive<const Data &, const Data &> {
  void operator ()(const Data &clone_spec, const Data &field_list_id) {
    Field &f_clone_spec = get_field("standard_metadata.clone_spec");
    f_clone_spec.shift_left(field_list_id, 16);
    f_clone_spec.add(f_clone_spec, clone_spec);
  }
};

REGISTER_PRIMITIVE(clone_egress_pkt_to_egress);

class resubmit : public ActionPrimitive<const Data &> {
  void operator ()(const Data &field_list_id) {
    if (get_phv().has_field("intrinsic_metadata.resubmit_flag")) {
      get_phv().get_field("intrinsic_metadata.resubmit_flag")
          .set(field_list_id);
    }
  }
};

REGISTER_PRIMITIVE(resubmit);

class recirculate : public ActionPrimitive<const Data &> {
  void operator ()(const Data &field_list_id) {
    if (get_phv().has_field("intrinsic_metadata.recirculate_flag")) {
      get_phv().get_field("intrinsic_metadata.recirculate_flag")
          .set(field_list_id);
    }
  }
};

REGISTER_PRIMITIVE(recirculate);

class modify_field_with_hash_based_offset
  : public ActionPrimitive<Data &, const Data &,
                           const NamedCalculation &, const Data &> {
  void operator ()(Data &dst, const Data &base,
                   const NamedCalculation &hash, const Data &size) {
    uint64_t v =
      (hash.output(get_packet()) % size.get<uint64_t>()) + base.get<uint64_t>();
    dst.set(v);
  }
};

REGISTER_PRIMITIVE(modify_field_with_hash_based_offset);

class no_op : public ActionPrimitive<> {
  void operator ()() {
    // nothing
  }
};

REGISTER_PRIMITIVE(no_op);

class execute_meter
  : public ActionPrimitive<MeterArray &, const Data &, Field &> {
  void operator ()(MeterArray &meter_array, const Data &idx, Field &dst) {
    dst.set(meter_array.execute_meter(get_packet(), idx.get_uint()));
  }
};

REGISTER_PRIMITIVE(execute_meter);

class count : public ActionPrimitive<CounterArray &, const Data &> {
  void operator ()(CounterArray &counter_array, const Data &idx) {
    counter_array.get_counter(idx.get_uint()).increment_counter(get_packet());
  }
};

REGISTER_PRIMITIVE(count);

class register_read
  : public ActionPrimitive<Field &, const RegisterArray &, const Data &> {
  void operator ()(Field &dst, const RegisterArray &src, const Data &idx) {
    dst.set(src[idx.get_uint()]);
  }
};

REGISTER_PRIMITIVE(register_read);

class register_write
  : public ActionPrimitive<RegisterArray &, const Data &, const Data &> {
  void operator ()(RegisterArray &dst, const Data &idx, const Data &src) {
    dst[idx.get_uint()].set(src);
  }
};

REGISTER_PRIMITIVE(register_write);

// I cannot name this "truncate" and register it with the usual
// REGISTER_PRIMITIVE macro, because of a name conflict:
//
// In file included from /usr/include/boost/config/stdlib/libstdcpp3.hpp:77:0,
//   from /usr/include/boost/config.hpp:44,
//   from /usr/include/boost/cstdint.hpp:36,
//   from /usr/include/boost/multiprecision/number.hpp:9,
//   from /usr/include/boost/multiprecision/gmp.hpp:9,
//   from ../../src/bm_sim/include/bm_sim/bignum.h:25,
//   from ../../src/bm_sim/include/bm_sim/data.h:32,
//   from ../../src/bm_sim/include/bm_sim/fields.h:28,
//   from ../../src/bm_sim/include/bm_sim/phv.h:34,
//   from ../../src/bm_sim/include/bm_sim/actions.h:34,
//   from primitives.cpp:21:
//     /usr/include/unistd.h:993:12: note: declared here
//     extern int truncate (const char *__file, __off_t __length)
class truncate_ : public ActionPrimitive<const Data &> {
  void operator ()(const Data &truncated_length) {
    get_packet().truncate(truncated_length.get<size_t>());
  }
};

REGISTER_PRIMITIVE_W_NAME("truncate", truncate_);

class rohc_comp_header : public ActionPrimitive<> {
  void operator ()() {
    std::chrono::high_resolution_clock::time_point t1 =
        std::chrono::high_resolution_clock::now();

    PHV* phv = get_packet().get_phv();
    size_t uncomp_headers_size = 0;
    size_t first_header_size =	 0;

    // Get the headers to compress skipping the first one
    std::vector<Header*> uncomp_headers;
    bool first_header = true;
    for (auto it = phv->header_begin(); it != phv->header_end(); ++it) {
      const Header &header = *it;
      if (header.is_valid() && !header.is_metadata()) {
	if(!first_header) {
          uncomp_headers_size += header.get_nbytes_packet();
          uncomp_headers.push_back((Header*) &header);
        }
        else {
          first_header = false;
          first_header_size = header.get_nbytes_packet();
        }
      }
    }
    size_t payload_size = phv->get_field("standard_metadata.packet_length")
        .get_uint() - first_header_size - uncomp_headers_size;
    size_t comp_header_size = 0;

    unsigned char *uncomp_buff = new unsigned char [uncomp_headers_size];
    unsigned char *comp_buff = new unsigned char [uncomp_headers_size
                                                  + payload_size + 2];

    // Initialize the compression data structures
    int index_comp_buff = 0;
    for(auto h : uncomp_headers) {
      for (size_t f = 0; f < h->size(); ++f) {
	  const char* data = h->get_field(f).get_bytes().data();
	    for (int i = 0; i < (int) h->get_field(f).get_bytes().size(); ++i) {
	      uncomp_buff[index_comp_buff] = *data;
	      ++index_comp_buff;
          ++data;
        }
      }
      // Mark headers invalid so they won't be serialized
      h->mark_invalid();
    }

    // Perform the header decompression
    rohc_c_ent.compress_header(
        comp_buff,
        uncomp_buff,
        &comp_header_size,
        (size_t) uncomp_headers_size + payload_size);

    comp_header_size -= payload_size;

    // Positionate the head of the buffer to put the compressed header
    // inside the payload
    char *payload_start = get_packet().prepend(comp_header_size);
    // Overwrite the packet headers with the compressed one
    for (int i = 0; i < (int)comp_header_size; ++i){
      payload_start[i] = comp_buff[i];
    }

    std::chrono::high_resolution_clock::time_point t2 =
        std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>
        (t2 - t1).count();
    std::cout << "Compression execution time: " << duration << " useconds\n";

  }
};

REGISTER_PRIMITIVE(rohc_comp_header);

#define EXTRA_LENGHT_UNCOMP 80

class rohc_decomp_header : public ActionPrimitive<> {
  void operator ()() {
    std::chrono::high_resolution_clock::time_point t1 =
        std::chrono::high_resolution_clock::now();

    // Calculate the size of all real header (not metadata) except the first one
    PHV* phv = get_packet().get_phv();
    std::vector<Header*> extracted_headers;
    size_t headers_size = 0;
    for (auto it = phv->header_begin(); it != phv->header_end(); ++it) {
      const Header &header = *it;
      if (header.is_valid() && !header.is_metadata()) {
        extracted_headers.push_back((Header*) &header);
        headers_size += header.get_nbytes_packet();
      }
    }

    size_t comp_header_size = phv->get_field("standard_metadata.packet_length")
        .get_uint() - headers_size;
    size_t uncomp_header_size = 0;
    unsigned char *comp_buff = new unsigned char [comp_header_size];
    unsigned char *uncomp_buff = new unsigned char [comp_header_size +
                                                    EXTRA_LENGHT_UNCOMP];

    // Initialize the decompression data structures
    int index_comp_buff = 0;
    const char *c = get_packet().prepend(0);
    for (int i = 0; i < (int) comp_header_size ; ++i) {
      comp_buff[index_comp_buff] = *c;
      ++index_comp_buff;
      ++c;
    }

    // Perform the header decompression
    rohc_d_ent.decompress_header(comp_buff,
                                 uncomp_buff,
                                 (size_t) comp_header_size,
                                 &uncomp_header_size);

    // Remove the compressed header inside the payload
    get_packet().remove(comp_header_size);
    // Positionate the head of the buffer to put the uncompressed
    // header inside the payload
    char *payload_start = get_packet().prepend(uncomp_header_size);
    // Overwrite the packet headers with the uncompressed one
    for (int i = 0; i < (int) uncomp_header_size; ++i)
      payload_start[i] = uncomp_buff[i];

    for (int i = extracted_headers.size() - 1; i >= 0; --i) {
      payload_start = get_packet().prepend(extracted_headers[i]->
          get_nbytes_packet());
      extracted_headers[i]->deparse(payload_start);
      extracted_headers[i]->mark_invalid();
    }

    std::chrono::high_resolution_clock::time_point t2 =
        std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>
        (t2 - t1).count();
    std::cout << "Decompression execution time: " << duration << " useconds\n";

  }
};

REGISTER_PRIMITIVE(rohc_decomp_header);

class modify_and_resubmit : public ActionPrimitive<const Data &> {
  void operator ()(const Data &field_list_id) {
    if (get_phv().has_field("intrinsic_metadata.modify_and_resubmit_flag")) {
      get_phv().get_field("intrinsic_metadata.modify_and_resubmit_flag")
          .set(field_list_id);
    }
  }
};

REGISTER_PRIMITIVE(modify_and_resubmit);

// dummy function, which ensures that this unit is not discarded by the linker
// it is being called by the constructor of SimpleSwitch
// the previous alternative was to have all the primitives in a header file (the
// primitives could also be placed in simple_switch.cpp directly), but I need
// this dummy function if I want to keep the primitives in their own file
int import_primitives() {
  return 0;
}
