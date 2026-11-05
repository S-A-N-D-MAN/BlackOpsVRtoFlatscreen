// SPDX-License-Identifier: GPL-3.0-only
#include "loader/native_span_guard.hpp"
// Generated from independently pinned49-guard native evidence.
// Existing normal CALL677856 is already guarded/installed by weapon_probe.
inline bool animation_native_guards() noexcept {
    __try {
        return true
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c392d), 5, "eab9e310cd3941ccff3e35cd0b91eb378a0932b48d435a936a66c02aaef9d150") // frontend_update_animation
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c3b10), 5, "6a0570806f233a32b9a4f1e38c1d8e4e461d63dc1f422c3dcff4984ce288f5c5") // frontend_add_view_weapon
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4b6951), 5, "610b7cb50c8d5aa13ac3d157e6fccf068111e09fbb942f212918a6bd0359199f") // run_weapon_xmodel_anims
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4b698e), 5, "b0dfd64786edd28e8003fc75cc6ebc1550065e77c8de7ad33affcfcba032758d") // update_client_info
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4b6997), 5, "05115abb02b59c4b66be4bdc47ccb422e16b70c33192886053996b49b8cd5047") // process_weapon_notes
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5de740), 5, "cbbd67b2a913e136ffa6386bc8cfc5b45b43c89bd87abbe9a0c32910e9509ffa") // advance_tree_weights
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5de763), 5, "3fc41304fd09846a7217c0edf8a85d55fb977ac4de6f9b397d1e54bc5c97f820") // advance_tree_time
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x50b838), 5, "baba6c9899e84709d6bb4b2d825b1de22f14459ef23075580329c1b74c776792") // normal_update_pose
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x50b847), 5, "c7298ff0665ec9bbd0a836a04a4b3b6e4d455e4782040da4aa99d35a8b988236") // normal_minigun
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x50b852), 5, "1abe953269a3ebb1732affef39e2eeea37afb72171753bf5f36f76364e5af9b5") // normal_stack_counter
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x6766ea), 5, "8b0ecfba953fd58536fc339f1efc46c5f9fa043b410b47c49472b4333d38bcef") // pose_lock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x6766f0), 5, "77a87436e8e1159bef8c7f99d79c6054665600375f016fd9bfccb259ae1f19f7") // pose_clear_part_bits
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x6766f6), 5, "9c076e05bcef33ada7cea5005b8f36ca3cce373df6ab3bc31085127569360a97") // pose_unlock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x484e54), 8, "50f8a648f572508aeb2086adc00013d5e569d5b54529882a9c51bdfd14f8c7b6") // clear_60_bytes_only
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5d73a4), 14, "832183dea531469ae9d52e56e65e8654215976c39f5a1b747fc530394ad3dbfb") // skeleton_timestamp_mat_check
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5d73b8), 8, "1424c15b5bf657264a0365519a0b0a7256b99b27196833fc15adc339744c1dbe") // mismatched_timestamp_clear_68
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x477a7c), 7, "c8c01c603bc6566cf30493c6421e84873c3b2e7f909c1bdd702977187f3c1555") // create_skel_only_assigns_pointer_clock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e18a), 5, "5f8aad029cae1ede53a7c6f8549908344e7d22d0e1d29b338d4176e85b5ea486") // bone_lock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e19a), 5, "734e5bf34ca254bf055cd4c67609714d94c6f7159e58fa6039be6bb53bbb721d") // bone_cache_create
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e1bc), 5, "38e1d2daa866651c0e6998f478c3a7b834e7366a4774b0505e348d60769c57b4") // bone_hierarchy
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e1cc), 5, "049d142b6afb7a2047c6bce0ecd71c486c8d86aae0eeb097256a5df5ebbe2a38") // bone_controllers
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e1d7), 5, "ecfed1cf60399e2aef14532b854618f4f2b665da53b875f5bfb91159b683bda3") // bone_skeleton
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x47e1dd), 5, "63b0a0b27eda8de4451e7555081fc05fe12c900596d0a1affad1d77ee97b7fad") // bone_unlock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x504985), 4, "194fca894f78fab36970d8e71fe63f23cf94d0359c5aafe99ef5c610376437fa") // lock_cmpxchg
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x54d484), 7, "4cdd0585a9c828821a146ecbb0c820ef8b5fbfda2e3a2043ddc96dbc5da88fe9") // unlock_word
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x583640), 18, "05b5db7204df38f14785b3b38736b2186bb74541696fb870aaaac9236a4a5382") // current_clock
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c7ddd), 10, "a54d0de7f81bc5eaeb06284d337be7f85564999ef0489a9ab0f61639fecbb1e5") // controller_viewmodel_type
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c7ed4), 6, "64f6b596eae28d3f962d244bb731232a9b242a0d1c7de0e095b9c7ee5f8c2ccd") // controller_destructible_flag
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c7ef0), 5, "b7a1aa6115ee719601276fec6be0b51be2593273bb41fd5fae2b7cc235bf077d") // controller_base_origin
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5c7ef8), 12, "36ecd5a2546c1daa0cf0a3596c96372c82f72c829ff7d9e9670987029a333399") // controller_ragdoll_flag_handle
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x64f871), 7, "a5cde0f6178b32efdfe4d1f0d933876beba959f4a8d8983fe5382ce9added0ab") // initialize_viewmodel_type
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x5ceb07), 6, "e3c110ae67617ef7e668013202ecaf921ba6413243b5e60646cbe59cfc46cc7c") // minigun_fire_type_gate
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x662a37), 10, "9a566f4d7ad37f4524178f997dc3952f451f56564ce0904454e9f08c20763b80") // stack_counter_fire_type_gate
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x608016), 7, "64bf63d8d0c3d0158262faeac0aa368e13d327a7123c4512a6178205d21e1b1f") // returned_matrix_stride
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x59c4a0), 8, "baaee2b64a30d7857ba4fd8ccd0862597f979d456dfa340639359dffed61e08f") // getter_only_mat_pointer
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4bb306), 5, "d81864abe6629a5ae8734cb25e3e290e568691b9c593c9e81b687475325c8efd") // copied_matrix_converter_call
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x68e5fc), 5, "311e7fa51284e6827a500ceb044ceb3346ed0743f15c0c1eac7ce309eda208c6") // matrix_weight_field
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4bb30b), 3, "6d1efb147a9081b12760505f6a20882bfb044e166a67b4cc5c0881e8d47410e1") // matrix_translation_x
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4bb317), 3, "ce1aee2a9ea8f81c0daf5f09739ac08dc14ce970b765e0f23176bbe901741326") // matrix_translation_y
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4bb31d), 3, "6a0cacda33be3ac6b29ad5b3daacf9159c1f6d4055fdf5bc7749d3c01feee711") // matrix_translation_z
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x782f03), 10, "a85daeeb32ee3fef8e94f64f7e5c65b008d010af1940c5dd64c88781acf32ebc") // base_pose_angles_to_quat
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x782fa1), 5, "f0ab59bff09bedc4220134fd65d341f2e2c40d7a87f5ea1c01f8eab3a05bf353") // base_pose_origin_x
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x782fac), 5, "31aa12b86ed566682374bbfe122ce83f905652f32e8dfcb91f6ea16ed4de6d57") // base_pose_origin_y
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x782fb7), 5, "5040e086f22e8619d213d02eacafc4e821c670a15faeb39fc8dcec51c252d969") // base_pose_origin_z
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x69f3cc), 5, "5b0f9c7125ab30555e1395f10449fe72bc8db557304ea6fd16778665c26e84f5") // angles_to_axis
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x69f3db), 5, "e314fb465ff5cb244193cb922196d12a2af8c3041c2d4d3dff23d681f3b543b0") // axis_to_quat
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x4c2ead), 5, "475ce4596fa40bc93916485013be9819fb3eebb8b50e3a34191658ea4e246186") // cache_hit_computed_bit_call
            && !bovr::native_guard::compare(reinterpret_cast<void*>(0x455c50), 34, "1696ab7c00a6cb5c09c8fdb927b9303b1bd008622b2dd8377ee9b9f9176af46a") // computed_bone_getter_complete
            ;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
