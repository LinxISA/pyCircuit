// pyCircuit C++ emission (prototype)
#include <pyc/cpp/pyc_sim.hpp>

namespace pyc::gen {

struct calculator {
  pyc::cpp::Wire<1> clk{};
  pyc::cpp::Wire<1> rst{};
  pyc::cpp::Wire<5> key{};
  pyc::cpp::Wire<1> key_press{};
  pyc::cpp::Wire<64> display{};
  pyc::cpp::Wire<1> display_neg{};
  pyc::cpp::Wire<1> display_err{};
  pyc::cpp::Wire<4> display_dp{};
  pyc::cpp::Wire<3> op_pending{};

  pyc::cpp::Wire<64> accum{};
  pyc::cpp::Wire<4> accum_dp{};
  pyc::cpp::Wire<1> accum_neg{};
  pyc::cpp::Wire<64> align_pow10{};
  pyc::cpp::Wire<4> alu_dp{};
  pyc::cpp::Wire<1> alu_neg{};
  pyc::cpp::Wire<64> alu_val{};
  pyc::cpp::Wire<5> digit_cnt{};
  pyc::cpp::Wire<64> display_2{};
  pyc::cpp::Wire<4> dp{};
  pyc::cpp::Wire<1> err{};
  pyc::cpp::Wire<1> has_dot{};
  pyc::cpp::Wire<1> neg{};
  pyc::cpp::Wire<3> new_op{};
  pyc::cpp::Wire<3> op{};
  pyc::cpp::Wire<64> pyc_add_137{};
  pyc::cpp::Wire<64> pyc_add_138{};
  pyc::cpp::Wire<64> pyc_add_178{};
  pyc::cpp::Wire<5> pyc_add_184{};
  pyc::cpp::Wire<5> pyc_add_223{};
  pyc::cpp::Wire<5> pyc_add_279{};
  pyc::cpp::Wire<4> pyc_add_281{};
  pyc::cpp::Wire<1> pyc_and_101{};
  pyc::cpp::Wire<1> pyc_and_106{};
  pyc::cpp::Wire<1> pyc_and_108{};
  pyc::cpp::Wire<1> pyc_and_110{};
  pyc::cpp::Wire<1> pyc_and_112{};
  pyc::cpp::Wire<1> pyc_and_114{};
  pyc::cpp::Wire<1> pyc_and_116{};
  pyc::cpp::Wire<1> pyc_and_252{};
  pyc::cpp::Wire<1> pyc_and_270{};
  pyc::cpp::Wire<1> pyc_and_272{};
  pyc::cpp::Wire<1> pyc_and_277{};
  pyc::cpp::Wire<1> pyc_and_283{};
  pyc::cpp::Wire<1> pyc_and_285{};
  pyc::cpp::Wire<1> pyc_and_288{};
  pyc::cpp::Wire<1> pyc_and_290{};
  pyc::cpp::Wire<1> pyc_and_293{};
  pyc::cpp::Wire<1> pyc_and_295{};
  pyc::cpp::Wire<1> pyc_and_301{};
  pyc::cpp::Wire<1> pyc_and_303{};
  pyc::cpp::Wire<1> pyc_and_307{};
  pyc::cpp::Wire<1> pyc_and_311{};
  pyc::cpp::Wire<1> pyc_and_315{};
  pyc::cpp::Wire<1> pyc_and_317{};
  pyc::cpp::Wire<1> pyc_and_320{};
  pyc::cpp::Wire<1> pyc_and_322{};
  pyc::cpp::Wire<1> pyc_and_325{};
  pyc::cpp::Wire<1> pyc_and_327{};
  pyc::cpp::Wire<1> pyc_and_330{};
  pyc::cpp::Wire<1> pyc_and_332{};
  pyc::cpp::Wire<1> pyc_and_335{};
  pyc::cpp::Wire<1> pyc_and_337{};
  pyc::cpp::Wire<1> pyc_and_340{};
  pyc::cpp::Wire<1> pyc_and_342{};
  pyc::cpp::Wire<1> pyc_and_345{};
  pyc::cpp::Wire<1> pyc_and_347{};
  pyc::cpp::Wire<1> pyc_and_350{};
  pyc::cpp::Wire<1> pyc_and_354{};
  pyc::cpp::Wire<1> pyc_and_360{};
  pyc::cpp::Wire<1> pyc_and_364{};
  pyc::cpp::Wire<1> pyc_and_368{};
  pyc::cpp::Wire<1> pyc_and_376{};
  pyc::cpp::Wire<1> pyc_and_380{};
  pyc::cpp::Wire<1> pyc_and_384{};
  pyc::cpp::Wire<1> pyc_and_388{};
  pyc::cpp::Wire<1> pyc_and_390{};
  pyc::cpp::Wire<1> pyc_and_93{};
  pyc::cpp::Wire<1> pyc_and_95{};
  pyc::cpp::Wire<1> pyc_and_97{};
  pyc::cpp::Wire<1> pyc_and_99{};
  pyc::cpp::Wire<1> pyc_comb_117{};
  pyc::cpp::Wire<1> pyc_comb_118{};
  pyc::cpp::Wire<1> pyc_comb_119{};
  pyc::cpp::Wire<1> pyc_comb_120{};
  pyc::cpp::Wire<1> pyc_comb_121{};
  pyc::cpp::Wire<1> pyc_comb_122{};
  pyc::cpp::Wire<1> pyc_comb_123{};
  pyc::cpp::Wire<1> pyc_comb_124{};
  pyc::cpp::Wire<1> pyc_comb_125{};
  pyc::cpp::Wire<1> pyc_comb_126{};
  pyc::cpp::Wire<1> pyc_comb_127{};
  pyc::cpp::Wire<1> pyc_comb_128{};
  pyc::cpp::Wire<1> pyc_comb_149{};
  pyc::cpp::Wire<1> pyc_comb_150{};
  pyc::cpp::Wire<1> pyc_comb_151{};
  pyc::cpp::Wire<1> pyc_comb_152{};
  pyc::cpp::Wire<64> pyc_comb_153{};
  pyc::cpp::Wire<64> pyc_comb_154{};
  pyc::cpp::Wire<1> pyc_comb_155{};
  pyc::cpp::Wire<1> pyc_comb_156{};
  pyc::cpp::Wire<64> pyc_comb_157{};
  pyc::cpp::Wire<1> pyc_comb_158{};
  pyc::cpp::Wire<1> pyc_comb_159{};
  pyc::cpp::Wire<4> pyc_comb_160{};
  pyc::cpp::Wire<1> pyc_comb_189{};
  pyc::cpp::Wire<1> pyc_comb_190{};
  pyc::cpp::Wire<1> pyc_comb_191{};
  pyc::cpp::Wire<1> pyc_comb_192{};
  pyc::cpp::Wire<1> pyc_comb_193{};
  pyc::cpp::Wire<1> pyc_comb_194{};
  pyc::cpp::Wire<1> pyc_comb_195{};
  pyc::cpp::Wire<1> pyc_comb_196{};
  pyc::cpp::Wire<4> pyc_comb_197{};
  pyc::cpp::Wire<64> pyc_comb_198{};
  pyc::cpp::Wire<64> pyc_comb_199{};
  pyc::cpp::Wire<64> pyc_comb_200{};
  pyc::cpp::Wire<1> pyc_comb_201{};
  pyc::cpp::Wire<5> pyc_comb_202{};
  pyc::cpp::Wire<5> pyc_comb_203{};
  pyc::cpp::Wire<5> pyc_comb_204{};
  pyc::cpp::Wire<1> pyc_comb_205{};
  pyc::cpp::Wire<4> pyc_comb_206{};
  pyc::cpp::Wire<1> pyc_comb_229{};
  pyc::cpp::Wire<1> pyc_comb_230{};
  pyc::cpp::Wire<1> pyc_comb_231{};
  pyc::cpp::Wire<1> pyc_comb_232{};
  pyc::cpp::Wire<1> pyc_comb_233{};
  pyc::cpp::Wire<1> pyc_comb_234{};
  pyc::cpp::Wire<1> pyc_comb_235{};
  pyc::cpp::Wire<1> pyc_comb_236{};
  pyc::cpp::Wire<64> pyc_comb_237{};
  pyc::cpp::Wire<4> pyc_comb_238{};
  pyc::cpp::Wire<1> pyc_comb_239{};
  pyc::cpp::Wire<64> pyc_comb_240{};
  pyc::cpp::Wire<4> pyc_comb_241{};
  pyc::cpp::Wire<1> pyc_comb_256{};
  pyc::cpp::Wire<64> pyc_comb_257{};
  pyc::cpp::Wire<1> pyc_comb_258{};
  pyc::cpp::Wire<64> pyc_comb_259{};
  pyc::cpp::Wire<1> pyc_comb_260{};
  pyc::cpp::Wire<1> pyc_comb_261{};
  pyc::cpp::Wire<1> pyc_comb_262{};
  pyc::cpp::Wire<1> pyc_comb_263{};
  pyc::cpp::Wire<64> pyc_comb_264{};
  pyc::cpp::Wire<1> pyc_comb_265{};
  pyc::cpp::Wire<1> pyc_comb_274{};
  pyc::cpp::Wire<1> pyc_comb_275{};
  pyc::cpp::Wire<1> pyc_comb_297{};
  pyc::cpp::Wire<1> pyc_comb_298{};
  pyc::cpp::Wire<1> pyc_comb_308{};
  pyc::cpp::Wire<1> pyc_comb_312{};
  pyc::cpp::Wire<1> pyc_comb_356{};
  pyc::cpp::Wire<1> pyc_comb_357{};
  pyc::cpp::Wire<1> pyc_comb_365{};
  pyc::cpp::Wire<1> pyc_comb_369{};
  pyc::cpp::Wire<1> pyc_comb_377{};
  pyc::cpp::Wire<1> pyc_comb_381{};
  pyc::cpp::Wire<1> pyc_comb_385{};
  pyc::cpp::Wire<1> pyc_comb_401{};
  pyc::cpp::Wire<64> pyc_comb_402{};
  pyc::cpp::Wire<64> pyc_comb_410{};
  pyc::cpp::Wire<2> pyc_comb_421{};
  pyc::cpp::Wire<3> pyc_comb_430{};
  pyc::cpp::Wire<1> pyc_comb_442{};
  pyc::cpp::Wire<1> pyc_comb_448{};
  pyc::cpp::Wire<5> pyc_comb_46{};
  pyc::cpp::Wire<5> pyc_comb_461{};
  pyc::cpp::Wire<1> pyc_comb_469{};
  pyc::cpp::Wire<1> pyc_comb_47{};
  pyc::cpp::Wire<5> pyc_comb_48{};
  pyc::cpp::Wire<4> pyc_comb_481{};
  pyc::cpp::Wire<4> pyc_comb_489{};
  pyc::cpp::Wire<1> pyc_comb_49{};
  pyc::cpp::Wire<64> pyc_comb_50{};
  pyc::cpp::Wire<1> pyc_comb_503{};
  pyc::cpp::Wire<3> pyc_comb_509{};
  pyc::cpp::Wire<4> pyc_comb_51{};
  pyc::cpp::Wire<64> pyc_comb_518{};
  pyc::cpp::Wire<5> pyc_comb_52{};
  pyc::cpp::Wire<64> pyc_comb_527{};
  pyc::cpp::Wire<64> pyc_comb_53{};
  pyc::cpp::Wire<64> pyc_comb_531{};
  pyc::cpp::Wire<1> pyc_comb_535{};
  pyc::cpp::Wire<4> pyc_comb_538{};
  pyc::cpp::Wire<64> pyc_comb_54{};
  pyc::cpp::Wire<4> pyc_comb_55{};
  pyc::cpp::Wire<64> pyc_comb_56{};
  pyc::cpp::Wire<4> pyc_comb_57{};
  pyc::cpp::Wire<64> pyc_comb_58{};
  pyc::cpp::Wire<4> pyc_comb_59{};
  pyc::cpp::Wire<64> pyc_comb_60{};
  pyc::cpp::Wire<4> pyc_comb_61{};
  pyc::cpp::Wire<64> pyc_comb_62{};
  pyc::cpp::Wire<4> pyc_comb_63{};
  pyc::cpp::Wire<64> pyc_comb_64{};
  pyc::cpp::Wire<4> pyc_comb_65{};
  pyc::cpp::Wire<64> pyc_comb_66{};
  pyc::cpp::Wire<4> pyc_comb_67{};
  pyc::cpp::Wire<64> pyc_comb_68{};
  pyc::cpp::Wire<64> pyc_comb_69{};
  pyc::cpp::Wire<4> pyc_comb_70{};
  pyc::cpp::Wire<2> pyc_comb_71{};
  pyc::cpp::Wire<2> pyc_comb_72{};
  pyc::cpp::Wire<2> pyc_comb_73{};
  pyc::cpp::Wire<2> pyc_comb_74{};
  pyc::cpp::Wire<3> pyc_comb_75{};
  pyc::cpp::Wire<3> pyc_comb_76{};
  pyc::cpp::Wire<3> pyc_comb_77{};
  pyc::cpp::Wire<3> pyc_comb_78{};
  pyc::cpp::Wire<3> pyc_comb_79{};
  pyc::cpp::Wire<5> pyc_comb_80{};
  pyc::cpp::Wire<5> pyc_comb_81{};
  pyc::cpp::Wire<5> pyc_comb_82{};
  pyc::cpp::Wire<5> pyc_comb_83{};
  pyc::cpp::Wire<5> pyc_comb_84{};
  pyc::cpp::Wire<5> pyc_comb_85{};
  pyc::cpp::Wire<5> pyc_comb_86{};
  pyc::cpp::Wire<5> pyc_comb_87{};
  pyc::cpp::Wire<5> pyc_comb_88{};
  pyc::cpp::Wire<5> pyc_comb_89{};
  pyc::cpp::Wire<5> pyc_comb_90{};
  pyc::cpp::Wire<5> pyc_constant_1{};
  pyc::cpp::Wire<4> pyc_constant_10{};
  pyc::cpp::Wire<64> pyc_constant_11{};
  pyc::cpp::Wire<4> pyc_constant_12{};
  pyc::cpp::Wire<64> pyc_constant_13{};
  pyc::cpp::Wire<4> pyc_constant_14{};
  pyc::cpp::Wire<64> pyc_constant_15{};
  pyc::cpp::Wire<4> pyc_constant_16{};
  pyc::cpp::Wire<64> pyc_constant_17{};
  pyc::cpp::Wire<4> pyc_constant_18{};
  pyc::cpp::Wire<64> pyc_constant_19{};
  pyc::cpp::Wire<1> pyc_constant_2{};
  pyc::cpp::Wire<4> pyc_constant_20{};
  pyc::cpp::Wire<64> pyc_constant_21{};
  pyc::cpp::Wire<4> pyc_constant_22{};
  pyc::cpp::Wire<64> pyc_constant_23{};
  pyc::cpp::Wire<64> pyc_constant_24{};
  pyc::cpp::Wire<4> pyc_constant_25{};
  pyc::cpp::Wire<2> pyc_constant_26{};
  pyc::cpp::Wire<2> pyc_constant_27{};
  pyc::cpp::Wire<2> pyc_constant_28{};
  pyc::cpp::Wire<2> pyc_constant_29{};
  pyc::cpp::Wire<5> pyc_constant_3{};
  pyc::cpp::Wire<3> pyc_constant_30{};
  pyc::cpp::Wire<3> pyc_constant_31{};
  pyc::cpp::Wire<3> pyc_constant_32{};
  pyc::cpp::Wire<3> pyc_constant_33{};
  pyc::cpp::Wire<3> pyc_constant_34{};
  pyc::cpp::Wire<5> pyc_constant_35{};
  pyc::cpp::Wire<5> pyc_constant_36{};
  pyc::cpp::Wire<5> pyc_constant_37{};
  pyc::cpp::Wire<5> pyc_constant_38{};
  pyc::cpp::Wire<5> pyc_constant_39{};
  pyc::cpp::Wire<1> pyc_constant_4{};
  pyc::cpp::Wire<5> pyc_constant_40{};
  pyc::cpp::Wire<5> pyc_constant_41{};
  pyc::cpp::Wire<5> pyc_constant_42{};
  pyc::cpp::Wire<5> pyc_constant_43{};
  pyc::cpp::Wire<5> pyc_constant_44{};
  pyc::cpp::Wire<5> pyc_constant_45{};
  pyc::cpp::Wire<64> pyc_constant_5{};
  pyc::cpp::Wire<4> pyc_constant_6{};
  pyc::cpp::Wire<5> pyc_constant_7{};
  pyc::cpp::Wire<64> pyc_constant_8{};
  pyc::cpp::Wire<64> pyc_constant_9{};
  pyc::cpp::Wire<1> pyc_eq_100{};
  pyc::cpp::Wire<1> pyc_eq_105{};
  pyc::cpp::Wire<1> pyc_eq_107{};
  pyc::cpp::Wire<1> pyc_eq_109{};
  pyc::cpp::Wire<1> pyc_eq_111{};
  pyc::cpp::Wire<1> pyc_eq_113{};
  pyc::cpp::Wire<1> pyc_eq_115{};
  pyc::cpp::Wire<1> pyc_eq_129{};
  pyc::cpp::Wire<1> pyc_eq_130{};
  pyc::cpp::Wire<1> pyc_eq_131{};
  pyc::cpp::Wire<1> pyc_eq_132{};
  pyc::cpp::Wire<1> pyc_eq_161{};
  pyc::cpp::Wire<1> pyc_eq_162{};
  pyc::cpp::Wire<1> pyc_eq_163{};
  pyc::cpp::Wire<1> pyc_eq_164{};
  pyc::cpp::Wire<1> pyc_eq_165{};
  pyc::cpp::Wire<1> pyc_eq_166{};
  pyc::cpp::Wire<1> pyc_eq_167{};
  pyc::cpp::Wire<1> pyc_eq_168{};
  pyc::cpp::Wire<1> pyc_eq_207{};
  pyc::cpp::Wire<1> pyc_eq_208{};
  pyc::cpp::Wire<1> pyc_eq_209{};
  pyc::cpp::Wire<1> pyc_eq_210{};
  pyc::cpp::Wire<1> pyc_eq_211{};
  pyc::cpp::Wire<1> pyc_eq_212{};
  pyc::cpp::Wire<1> pyc_eq_213{};
  pyc::cpp::Wire<1> pyc_eq_214{};
  pyc::cpp::Wire<1> pyc_eq_220{};
  pyc::cpp::Wire<1> pyc_eq_248{};
  pyc::cpp::Wire<1> pyc_eq_249{};
  pyc::cpp::Wire<1> pyc_eq_250{};
  pyc::cpp::Wire<1> pyc_eq_310{};
  pyc::cpp::Wire<1> pyc_eq_352{};
  pyc::cpp::Wire<1> pyc_eq_379{};
  pyc::cpp::Wire<1> pyc_eq_383{};
  pyc::cpp::Wire<1> pyc_eq_94{};
  pyc::cpp::Wire<1> pyc_eq_96{};
  pyc::cpp::Wire<1> pyc_eq_98{};
  pyc::cpp::Wire<64> pyc_mul_169{};
  pyc::cpp::Wire<64> pyc_mul_171{};
  pyc::cpp::Wire<64> pyc_mul_180{};
  pyc::cpp::Wire<64> pyc_mul_219{};
  pyc::cpp::Wire<4> pyc_mux_146{};
  pyc::cpp::Wire<4> pyc_mux_148{};
  pyc::cpp::Wire<64> pyc_mux_170{};
  pyc::cpp::Wire<64> pyc_mux_172{};
  pyc::cpp::Wire<4> pyc_mux_173{};
  pyc::cpp::Wire<64> pyc_mux_175{};
  pyc::cpp::Wire<64> pyc_mux_177{};
  pyc::cpp::Wire<4> pyc_mux_188{};
  pyc::cpp::Wire<64> pyc_mux_216{};
  pyc::cpp::Wire<4> pyc_mux_218{};
  pyc::cpp::Wire<64> pyc_mux_221{};
  pyc::cpp::Wire<4> pyc_mux_228{};
  pyc::cpp::Wire<64> pyc_mux_244{};
  pyc::cpp::Wire<64> pyc_mux_247{};
  pyc::cpp::Wire<64> pyc_mux_391{};
  pyc::cpp::Wire<64> pyc_mux_392{};
  pyc::cpp::Wire<64> pyc_mux_393{};
  pyc::cpp::Wire<64> pyc_mux_394{};
  pyc::cpp::Wire<64> pyc_mux_395{};
  pyc::cpp::Wire<64> pyc_mux_396{};
  pyc::cpp::Wire<64> pyc_mux_397{};
  pyc::cpp::Wire<64> pyc_mux_398{};
  pyc::cpp::Wire<64> pyc_mux_399{};
  pyc::cpp::Wire<64> pyc_mux_400{};
  pyc::cpp::Wire<64> pyc_mux_404{};
  pyc::cpp::Wire<64> pyc_mux_405{};
  pyc::cpp::Wire<64> pyc_mux_406{};
  pyc::cpp::Wire<64> pyc_mux_407{};
  pyc::cpp::Wire<64> pyc_mux_408{};
  pyc::cpp::Wire<64> pyc_mux_409{};
  pyc::cpp::Wire<2> pyc_mux_412{};
  pyc::cpp::Wire<2> pyc_mux_413{};
  pyc::cpp::Wire<2> pyc_mux_414{};
  pyc::cpp::Wire<2> pyc_mux_415{};
  pyc::cpp::Wire<2> pyc_mux_416{};
  pyc::cpp::Wire<2> pyc_mux_417{};
  pyc::cpp::Wire<2> pyc_mux_418{};
  pyc::cpp::Wire<2> pyc_mux_419{};
  pyc::cpp::Wire<2> pyc_mux_420{};
  pyc::cpp::Wire<3> pyc_mux_423{};
  pyc::cpp::Wire<3> pyc_mux_424{};
  pyc::cpp::Wire<3> pyc_mux_425{};
  pyc::cpp::Wire<3> pyc_mux_426{};
  pyc::cpp::Wire<3> pyc_mux_427{};
  pyc::cpp::Wire<3> pyc_mux_428{};
  pyc::cpp::Wire<3> pyc_mux_429{};
  pyc::cpp::Wire<1> pyc_mux_432{};
  pyc::cpp::Wire<1> pyc_mux_433{};
  pyc::cpp::Wire<1> pyc_mux_434{};
  pyc::cpp::Wire<1> pyc_mux_435{};
  pyc::cpp::Wire<1> pyc_mux_436{};
  pyc::cpp::Wire<1> pyc_mux_437{};
  pyc::cpp::Wire<1> pyc_mux_438{};
  pyc::cpp::Wire<1> pyc_mux_439{};
  pyc::cpp::Wire<1> pyc_mux_440{};
  pyc::cpp::Wire<1> pyc_mux_441{};
  pyc::cpp::Wire<1> pyc_mux_444{};
  pyc::cpp::Wire<1> pyc_mux_445{};
  pyc::cpp::Wire<1> pyc_mux_446{};
  pyc::cpp::Wire<1> pyc_mux_447{};
  pyc::cpp::Wire<5> pyc_mux_450{};
  pyc::cpp::Wire<5> pyc_mux_451{};
  pyc::cpp::Wire<5> pyc_mux_452{};
  pyc::cpp::Wire<5> pyc_mux_453{};
  pyc::cpp::Wire<5> pyc_mux_454{};
  pyc::cpp::Wire<5> pyc_mux_455{};
  pyc::cpp::Wire<5> pyc_mux_456{};
  pyc::cpp::Wire<5> pyc_mux_457{};
  pyc::cpp::Wire<5> pyc_mux_458{};
  pyc::cpp::Wire<5> pyc_mux_459{};
  pyc::cpp::Wire<5> pyc_mux_460{};
  pyc::cpp::Wire<1> pyc_mux_463{};
  pyc::cpp::Wire<1> pyc_mux_464{};
  pyc::cpp::Wire<1> pyc_mux_465{};
  pyc::cpp::Wire<1> pyc_mux_466{};
  pyc::cpp::Wire<1> pyc_mux_467{};
  pyc::cpp::Wire<1> pyc_mux_468{};
  pyc::cpp::Wire<4> pyc_mux_471{};
  pyc::cpp::Wire<4> pyc_mux_472{};
  pyc::cpp::Wire<4> pyc_mux_473{};
  pyc::cpp::Wire<4> pyc_mux_474{};
  pyc::cpp::Wire<4> pyc_mux_475{};
  pyc::cpp::Wire<4> pyc_mux_476{};
  pyc::cpp::Wire<4> pyc_mux_477{};
  pyc::cpp::Wire<4> pyc_mux_478{};
  pyc::cpp::Wire<4> pyc_mux_479{};
  pyc::cpp::Wire<4> pyc_mux_480{};
  pyc::cpp::Wire<4> pyc_mux_483{};
  pyc::cpp::Wire<4> pyc_mux_484{};
  pyc::cpp::Wire<4> pyc_mux_485{};
  pyc::cpp::Wire<4> pyc_mux_486{};
  pyc::cpp::Wire<4> pyc_mux_487{};
  pyc::cpp::Wire<4> pyc_mux_488{};
  pyc::cpp::Wire<1> pyc_mux_491{};
  pyc::cpp::Wire<1> pyc_mux_492{};
  pyc::cpp::Wire<1> pyc_mux_493{};
  pyc::cpp::Wire<1> pyc_mux_494{};
  pyc::cpp::Wire<1> pyc_mux_495{};
  pyc::cpp::Wire<1> pyc_mux_496{};
  pyc::cpp::Wire<1> pyc_mux_497{};
  pyc::cpp::Wire<1> pyc_mux_498{};
  pyc::cpp::Wire<1> pyc_mux_499{};
  pyc::cpp::Wire<1> pyc_mux_500{};
  pyc::cpp::Wire<1> pyc_mux_501{};
  pyc::cpp::Wire<1> pyc_mux_502{};
  pyc::cpp::Wire<3> pyc_mux_505{};
  pyc::cpp::Wire<3> pyc_mux_506{};
  pyc::cpp::Wire<3> pyc_mux_507{};
  pyc::cpp::Wire<3> pyc_mux_508{};
  pyc::cpp::Wire<64> pyc_mux_510{};
  pyc::cpp::Wire<64> pyc_mux_511{};
  pyc::cpp::Wire<64> pyc_mux_512{};
  pyc::cpp::Wire<64> pyc_mux_513{};
  pyc::cpp::Wire<64> pyc_mux_514{};
  pyc::cpp::Wire<64> pyc_mux_515{};
  pyc::cpp::Wire<64> pyc_mux_516{};
  pyc::cpp::Wire<64> pyc_mux_517{};
  pyc::cpp::Wire<64> pyc_mux_519{};
  pyc::cpp::Wire<64> pyc_mux_520{};
  pyc::cpp::Wire<64> pyc_mux_521{};
  pyc::cpp::Wire<64> pyc_mux_522{};
  pyc::cpp::Wire<64> pyc_mux_523{};
  pyc::cpp::Wire<64> pyc_mux_524{};
  pyc::cpp::Wire<64> pyc_mux_525{};
  pyc::cpp::Wire<64> pyc_mux_526{};
  pyc::cpp::Wire<64> pyc_mux_528{};
  pyc::cpp::Wire<64> pyc_mux_529{};
  pyc::cpp::Wire<64> pyc_mux_530{};
  pyc::cpp::Wire<1> pyc_mux_532{};
  pyc::cpp::Wire<1> pyc_mux_533{};
  pyc::cpp::Wire<1> pyc_mux_534{};
  pyc::cpp::Wire<4> pyc_mux_536{};
  pyc::cpp::Wire<4> pyc_mux_537{};
  pyc::cpp::Wire<1> pyc_not_226{};
  pyc::cpp::Wire<1> pyc_not_273{};
  pyc::cpp::Wire<1> pyc_not_296{};
  pyc::cpp::Wire<1> pyc_not_353{};
  pyc::cpp::Wire<1> pyc_not_355{};
  pyc::cpp::Wire<1> pyc_not_92{};
  pyc::cpp::Wire<1> pyc_or_102{};
  pyc::cpp::Wire<1> pyc_or_103{};
  pyc::cpp::Wire<1> pyc_or_104{};
  pyc::cpp::Wire<1> pyc_or_253{};
  pyc::cpp::Wire<1> pyc_or_268{};
  pyc::cpp::Wire<1> pyc_or_306{};
  pyc::cpp::Wire<1> pyc_or_363{};
  pyc::cpp::Wire<1> pyc_reg_266{};
  pyc::cpp::Wire<1> pyc_reg_267{};
  pyc::cpp::Wire<1> pyc_reg_269{};
  pyc::cpp::Wire<1> pyc_reg_271{};
  pyc::cpp::Wire<1> pyc_reg_276{};
  pyc::cpp::Wire<5> pyc_reg_278{};
  pyc::cpp::Wire<4> pyc_reg_280{};
  pyc::cpp::Wire<1> pyc_reg_282{};
  pyc::cpp::Wire<1> pyc_reg_284{};
  pyc::cpp::Wire<1> pyc_reg_286{};
  pyc::cpp::Wire<1> pyc_reg_287{};
  pyc::cpp::Wire<1> pyc_reg_289{};
  pyc::cpp::Wire<1> pyc_reg_291{};
  pyc::cpp::Wire<1> pyc_reg_292{};
  pyc::cpp::Wire<1> pyc_reg_294{};
  pyc::cpp::Wire<1> pyc_reg_299{};
  pyc::cpp::Wire<1> pyc_reg_300{};
  pyc::cpp::Wire<1> pyc_reg_302{};
  pyc::cpp::Wire<1> pyc_reg_304{};
  pyc::cpp::Wire<1> pyc_reg_305{};
  pyc::cpp::Wire<5> pyc_reg_309{};
  pyc::cpp::Wire<1> pyc_reg_313{};
  pyc::cpp::Wire<1> pyc_reg_314{};
  pyc::cpp::Wire<1> pyc_reg_316{};
  pyc::cpp::Wire<1> pyc_reg_318{};
  pyc::cpp::Wire<1> pyc_reg_319{};
  pyc::cpp::Wire<1> pyc_reg_321{};
  pyc::cpp::Wire<1> pyc_reg_323{};
  pyc::cpp::Wire<1> pyc_reg_324{};
  pyc::cpp::Wire<1> pyc_reg_326{};
  pyc::cpp::Wire<1> pyc_reg_328{};
  pyc::cpp::Wire<1> pyc_reg_329{};
  pyc::cpp::Wire<1> pyc_reg_331{};
  pyc::cpp::Wire<1> pyc_reg_333{};
  pyc::cpp::Wire<1> pyc_reg_334{};
  pyc::cpp::Wire<1> pyc_reg_336{};
  pyc::cpp::Wire<1> pyc_reg_338{};
  pyc::cpp::Wire<1> pyc_reg_339{};
  pyc::cpp::Wire<1> pyc_reg_341{};
  pyc::cpp::Wire<1> pyc_reg_343{};
  pyc::cpp::Wire<1> pyc_reg_344{};
  pyc::cpp::Wire<1> pyc_reg_346{};
  pyc::cpp::Wire<1> pyc_reg_348{};
  pyc::cpp::Wire<1> pyc_reg_349{};
  pyc::cpp::Wire<64> pyc_reg_351{};
  pyc::cpp::Wire<1> pyc_reg_358{};
  pyc::cpp::Wire<1> pyc_reg_359{};
  pyc::cpp::Wire<1> pyc_reg_361{};
  pyc::cpp::Wire<1> pyc_reg_362{};
  pyc::cpp::Wire<5> pyc_reg_366{};
  pyc::cpp::Wire<5> pyc_reg_370{};
  pyc::cpp::Wire<4> pyc_reg_372{};
  pyc::cpp::Wire<4> pyc_reg_374{};
  pyc::cpp::Wire<4> pyc_reg_378{};
  pyc::cpp::Wire<5> pyc_reg_382{};
  pyc::cpp::Wire<1> pyc_reg_386{};
  pyc::cpp::Wire<1> pyc_reg_387{};
  pyc::cpp::Wire<1> pyc_reg_389{};
  pyc::cpp::Wire<64> pyc_reg_403{};
  pyc::cpp::Wire<64> pyc_reg_411{};
  pyc::cpp::Wire<2> pyc_reg_422{};
  pyc::cpp::Wire<3> pyc_reg_431{};
  pyc::cpp::Wire<1> pyc_reg_443{};
  pyc::cpp::Wire<1> pyc_reg_449{};
  pyc::cpp::Wire<5> pyc_reg_462{};
  pyc::cpp::Wire<1> pyc_reg_470{};
  pyc::cpp::Wire<4> pyc_reg_482{};
  pyc::cpp::Wire<4> pyc_reg_490{};
  pyc::cpp::Wire<1> pyc_reg_504{};
  pyc::cpp::Wire<64> pyc_shli_135{};
  pyc::cpp::Wire<64> pyc_shli_136{};
  pyc::cpp::Wire<1> pyc_slt_242{};
  pyc::cpp::Wire<1> pyc_slt_245{};
  pyc::cpp::Wire<4> pyc_sub_144{};
  pyc::cpp::Wire<4> pyc_sub_145{};
  pyc::cpp::Wire<64> pyc_sub_174{};
  pyc::cpp::Wire<64> pyc_sub_176{};
  pyc::cpp::Wire<64> pyc_sub_179{};
  pyc::cpp::Wire<5> pyc_sub_186{};
  pyc::cpp::Wire<5> pyc_sub_224{};
  pyc::cpp::Wire<64> pyc_sub_243{};
  pyc::cpp::Wire<64> pyc_sub_246{};
  pyc::cpp::Wire<5> pyc_sub_371{};
  pyc::cpp::Wire<4> pyc_sub_373{};
  pyc::cpp::Wire<4> pyc_trunc_133{};
  pyc::cpp::Wire<4> pyc_trunc_187{};
  pyc::cpp::Wire<4> pyc_trunc_217{};
  pyc::cpp::Wire<4> pyc_trunc_227{};
  pyc::cpp::Wire<64> pyc_udiv_141{};
  pyc::cpp::Wire<64> pyc_udiv_215{};
  pyc::cpp::Wire<64> pyc_udiv_222{};
  pyc::cpp::Wire<64> pyc_udiv_254{};
  pyc::cpp::Wire<1> pyc_ult_139{};
  pyc::cpp::Wire<1> pyc_ult_140{};
  pyc::cpp::Wire<1> pyc_ult_142{};
  pyc::cpp::Wire<1> pyc_ult_143{};
  pyc::cpp::Wire<1> pyc_ult_147{};
  pyc::cpp::Wire<1> pyc_ult_185{};
  pyc::cpp::Wire<1> pyc_ult_225{};
  pyc::cpp::Wire<1> pyc_ult_251{};
  pyc::cpp::Wire<1> pyc_ult_255{};
  pyc::cpp::Wire<1> pyc_ult_367{};
  pyc::cpp::Wire<1> pyc_ult_375{};
  pyc::cpp::Wire<1> pyc_ult_91{};
  pyc::cpp::Wire<1> pyc_xor_181{};
  pyc::cpp::Wire<64> pyc_zext_134{};
  pyc::cpp::Wire<5> pyc_zext_182{};
  pyc::cpp::Wire<5> pyc_zext_183{};
  pyc::cpp::Wire<2> state{};
  pyc::cpp::Wire<64> trim_pow10{};

  pyc::cpp::pyc_reg<1> pyc_reg_266_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_267_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_269_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_271_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_276_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_278_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_280_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_282_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_284_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_286_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_287_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_289_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_291_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_292_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_294_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_299_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_300_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_302_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_304_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_305_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_309_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_313_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_314_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_316_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_318_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_319_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_321_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_323_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_324_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_326_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_328_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_329_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_331_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_333_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_334_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_336_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_338_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_339_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_341_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_343_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_344_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_346_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_348_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_349_inst;
  pyc::cpp::pyc_reg<64> pyc_reg_351_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_358_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_359_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_361_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_362_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_366_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_370_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_372_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_374_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_378_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_382_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_386_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_387_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_389_inst;
  pyc::cpp::pyc_reg<64> pyc_reg_403_inst;
  pyc::cpp::pyc_reg<64> pyc_reg_411_inst;
  pyc::cpp::pyc_reg<2> pyc_reg_422_inst;
  pyc::cpp::pyc_reg<3> pyc_reg_431_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_443_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_449_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_462_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_470_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_482_inst;
  pyc::cpp::pyc_reg<4> pyc_reg_490_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_504_inst;

  calculator() :
      pyc_reg_266_inst(clk, rst, pyc_comb_47, pyc_comb_149, pyc_comb_49, pyc_reg_266),
      pyc_reg_267_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_267),
      pyc_reg_269_inst(clk, rst, pyc_comb_47, pyc_comb_117, pyc_comb_49, pyc_reg_269),
      pyc_reg_271_inst(clk, rst, pyc_comb_47, pyc_comb_155, pyc_comb_49, pyc_reg_271),
      pyc_reg_276_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_276),
      pyc_reg_278_inst(clk, rst, pyc_comb_47, digit_cnt, pyc_comb_48, pyc_reg_278),
      pyc_reg_280_inst(clk, rst, pyc_comb_47, dp, pyc_comb_51, pyc_reg_280),
      pyc_reg_282_inst(clk, rst, pyc_comb_47, has_dot, pyc_comb_49, pyc_reg_282),
      pyc_reg_284_inst(clk, rst, pyc_comb_47, pyc_comb_156, pyc_comb_49, pyc_reg_284),
      pyc_reg_286_inst(clk, rst, pyc_comb_47, pyc_comb_117, pyc_comb_49, pyc_reg_286),
      pyc_reg_287_inst(clk, rst, pyc_comb_47, pyc_comb_150, pyc_comb_49, pyc_reg_287),
      pyc_reg_289_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_289),
      pyc_reg_291_inst(clk, rst, pyc_comb_47, pyc_comb_117, pyc_comb_49, pyc_reg_291),
      pyc_reg_292_inst(clk, rst, pyc_comb_47, pyc_comb_152, pyc_comb_49, pyc_reg_292),
      pyc_reg_294_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_294),
      pyc_reg_299_inst(clk, rst, pyc_comb_47, pyc_comb_128, pyc_comb_49, pyc_reg_299),
      pyc_reg_300_inst(clk, rst, pyc_comb_47, pyc_comb_298, pyc_comb_49, pyc_reg_300),
      pyc_reg_302_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_302),
      pyc_reg_304_inst(clk, rst, pyc_comb_47, pyc_comb_149, pyc_comb_49, pyc_reg_304),
      pyc_reg_305_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_305),
      pyc_reg_309_inst(clk, rst, pyc_comb_47, digit_cnt, pyc_comb_48, pyc_reg_309),
      pyc_reg_313_inst(clk, rst, pyc_comb_47, pyc_comb_128, pyc_comb_49, pyc_reg_313),
      pyc_reg_314_inst(clk, rst, pyc_comb_47, pyc_comb_150, pyc_comb_49, pyc_reg_314),
      pyc_reg_316_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_316),
      pyc_reg_318_inst(clk, rst, pyc_comb_47, pyc_comb_128, pyc_comb_49, pyc_reg_318),
      pyc_reg_319_inst(clk, rst, pyc_comb_47, pyc_comb_152, pyc_comb_49, pyc_reg_319),
      pyc_reg_321_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_321),
      pyc_reg_323_inst(clk, rst, pyc_comb_47, pyc_comb_122, pyc_comb_49, pyc_reg_323),
      pyc_reg_324_inst(clk, rst, pyc_comb_47, pyc_comb_149, pyc_comb_49, pyc_reg_324),
      pyc_reg_326_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_326),
      pyc_reg_328_inst(clk, rst, pyc_comb_47, pyc_comb_122, pyc_comb_49, pyc_reg_328),
      pyc_reg_329_inst(clk, rst, pyc_comb_47, pyc_comb_150, pyc_comb_49, pyc_reg_329),
      pyc_reg_331_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_331),
      pyc_reg_333_inst(clk, rst, pyc_comb_47, pyc_comb_122, pyc_comb_49, pyc_reg_333),
      pyc_reg_334_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_334),
      pyc_reg_336_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_336),
      pyc_reg_338_inst(clk, rst, pyc_comb_47, pyc_comb_122, pyc_comb_49, pyc_reg_338),
      pyc_reg_339_inst(clk, rst, pyc_comb_47, pyc_comb_152, pyc_comb_49, pyc_reg_339),
      pyc_reg_341_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_341),
      pyc_reg_343_inst(clk, rst, pyc_comb_47, pyc_comb_123, pyc_comb_49, pyc_reg_343),
      pyc_reg_344_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_344),
      pyc_reg_346_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_346),
      pyc_reg_348_inst(clk, rst, pyc_comb_47, pyc_comb_125, pyc_comb_49, pyc_reg_348),
      pyc_reg_349_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_349),
      pyc_reg_351_inst(clk, rst, pyc_comb_47, display_2, pyc_comb_53, pyc_reg_351),
      pyc_reg_358_inst(clk, rst, pyc_comb_47, pyc_comb_126, pyc_comb_49, pyc_reg_358),
      pyc_reg_359_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_359),
      pyc_reg_361_inst(clk, rst, pyc_comb_47, pyc_comb_149, pyc_comb_49, pyc_reg_361),
      pyc_reg_362_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_362),
      pyc_reg_366_inst(clk, rst, pyc_comb_47, digit_cnt, pyc_comb_48, pyc_reg_366),
      pyc_reg_370_inst(clk, rst, pyc_comb_47, digit_cnt, pyc_comb_48, pyc_reg_370),
      pyc_reg_372_inst(clk, rst, pyc_comb_47, dp, pyc_comb_51, pyc_reg_372),
      pyc_reg_374_inst(clk, rst, pyc_comb_47, dp, pyc_comb_51, pyc_reg_374),
      pyc_reg_378_inst(clk, rst, pyc_comb_47, dp, pyc_comb_51, pyc_reg_378),
      pyc_reg_382_inst(clk, rst, pyc_comb_47, digit_cnt, pyc_comb_48, pyc_reg_382),
      pyc_reg_386_inst(clk, rst, pyc_comb_47, pyc_comb_127, pyc_comb_49, pyc_reg_386),
      pyc_reg_387_inst(clk, rst, pyc_comb_47, pyc_comb_151, pyc_comb_49, pyc_reg_387),
      pyc_reg_389_inst(clk, rst, pyc_comb_47, pyc_comb_275, pyc_comb_49, pyc_reg_389),
      pyc_reg_403_inst(clk, rst, pyc_comb_47, pyc_comb_402, pyc_comb_53, pyc_reg_403),
      pyc_reg_411_inst(clk, rst, pyc_comb_47, pyc_comb_410, pyc_comb_53, pyc_reg_411),
      pyc_reg_422_inst(clk, rst, pyc_comb_47, pyc_comb_421, pyc_comb_74, pyc_reg_422),
      pyc_reg_431_inst(clk, rst, pyc_comb_47, pyc_comb_430, pyc_comb_79, pyc_reg_431),
      pyc_reg_443_inst(clk, rst, pyc_comb_47, pyc_comb_442, pyc_comb_49, pyc_reg_443),
      pyc_reg_449_inst(clk, rst, pyc_comb_47, pyc_comb_448, pyc_comb_49, pyc_reg_449),
      pyc_reg_462_inst(clk, rst, pyc_comb_47, pyc_comb_461, pyc_comb_48, pyc_reg_462),
      pyc_reg_470_inst(clk, rst, pyc_comb_47, pyc_comb_469, pyc_comb_49, pyc_reg_470),
      pyc_reg_482_inst(clk, rst, pyc_comb_47, pyc_comb_481, pyc_comb_51, pyc_reg_482),
      pyc_reg_490_inst(clk, rst, pyc_comb_47, pyc_comb_489, pyc_comb_51, pyc_reg_490),
      pyc_reg_504_inst(clk, rst, pyc_comb_47, pyc_comb_503, pyc_comb_49, pyc_reg_504) {
    eval();
  }

  inline void eval_comb_0() {
    pyc_ult_91 = pyc::cpp::Wire<1>((pyc_comb_90 < key) ? 1u : 0u);
    pyc_not_92 = (~pyc_ult_91);
    pyc_and_93 = (pyc_not_92 & key_press);
    pyc_eq_94 = pyc::cpp::Wire<1>((key == pyc_comb_89) ? 1u : 0u);
    pyc_and_95 = (pyc_eq_94 & key_press);
    pyc_eq_96 = pyc::cpp::Wire<1>((key == pyc_comb_88) ? 1u : 0u);
    pyc_and_97 = (pyc_eq_96 & key_press);
    pyc_eq_98 = pyc::cpp::Wire<1>((key == pyc_comb_87) ? 1u : 0u);
    pyc_and_99 = (pyc_eq_98 & key_press);
    pyc_eq_100 = pyc::cpp::Wire<1>((key == pyc_comb_86) ? 1u : 0u);
    pyc_and_101 = (pyc_eq_100 & key_press);
    pyc_or_102 = (pyc_and_95 | pyc_and_97);
    pyc_or_103 = (pyc_or_102 | pyc_and_99);
    pyc_or_104 = (pyc_or_103 | pyc_and_101);
    pyc_eq_105 = pyc::cpp::Wire<1>((key == pyc_comb_85) ? 1u : 0u);
    pyc_and_106 = (pyc_eq_105 & key_press);
    pyc_eq_107 = pyc::cpp::Wire<1>((key == pyc_comb_84) ? 1u : 0u);
    pyc_and_108 = (pyc_eq_107 & key_press);
    pyc_eq_109 = pyc::cpp::Wire<1>((key == pyc_comb_83) ? 1u : 0u);
    pyc_and_110 = (pyc_eq_109 & key_press);
    pyc_eq_111 = pyc::cpp::Wire<1>((key == pyc_comb_82) ? 1u : 0u);
    pyc_and_112 = (pyc_eq_111 & key_press);
    pyc_eq_113 = pyc::cpp::Wire<1>((key == pyc_comb_81) ? 1u : 0u);
    pyc_and_114 = (pyc_eq_113 & key_press);
    pyc_eq_115 = pyc::cpp::Wire<1>((key == pyc_comb_80) ? 1u : 0u);
    pyc_and_116 = (pyc_eq_115 & key_press);
    pyc_comb_117 = pyc_and_93;
    pyc_comb_118 = pyc_and_95;
    pyc_comb_119 = pyc_and_97;
    pyc_comb_120 = pyc_and_99;
    pyc_comb_121 = pyc_and_101;
    pyc_comb_122 = pyc_or_104;
    pyc_comb_123 = pyc_and_106;
    pyc_comb_124 = pyc_and_108;
    pyc_comb_125 = pyc_and_110;
    pyc_comb_126 = pyc_and_112;
    pyc_comb_127 = pyc_and_114;
    pyc_comb_128 = pyc_and_116;
  }

  inline void eval_comb_1() {
    pyc_eq_129 = pyc::cpp::Wire<1>((state == pyc_comb_74) ? 1u : 0u);
    pyc_eq_130 = pyc::cpp::Wire<1>((state == pyc_comb_73) ? 1u : 0u);
    pyc_eq_131 = pyc::cpp::Wire<1>((state == pyc_comb_72) ? 1u : 0u);
    pyc_eq_132 = pyc::cpp::Wire<1>((state == pyc_comb_71) ? 1u : 0u);
    pyc_trunc_133 = pyc::cpp::trunc<4, 5>(key);
    pyc_zext_134 = pyc::cpp::zext<64, 4>(pyc_trunc_133);
    pyc_shli_135 = pyc::cpp::shl<64>(display_2, 3u);
    pyc_shli_136 = pyc::cpp::shl<64>(display_2, 1u);
    pyc_add_137 = (pyc_shli_135 + pyc_shli_136);
    pyc_add_138 = (pyc_add_137 + pyc_zext_134);
    pyc_ult_139 = pyc::cpp::Wire<1>((digit_cnt < pyc_comb_83) ? 1u : 0u);
    pyc_ult_140 = pyc::cpp::Wire<1>((dp < pyc_comb_70) ? 1u : 0u);
    pyc_udiv_141 = pyc::cpp::udiv<64>(display_2, pyc_comb_69);
    pyc_ult_142 = pyc::cpp::Wire<1>((dp < accum_dp) ? 1u : 0u);
    pyc_ult_143 = pyc::cpp::Wire<1>((accum_dp < dp) ? 1u : 0u);
    pyc_sub_144 = (accum_dp - dp);
    pyc_sub_145 = (dp - accum_dp);
    pyc_mux_146 = (pyc_ult_142.toBool() ? pyc_sub_144 : pyc_sub_145);
    pyc_ult_147 = pyc::cpp::Wire<1>((pyc_comb_70 < pyc_mux_146) ? 1u : 0u);
    pyc_mux_148 = (pyc_ult_147.toBool() ? pyc_comb_70 : pyc_mux_146);
    pyc_comb_149 = pyc_eq_129;
    pyc_comb_150 = pyc_eq_130;
    pyc_comb_151 = pyc_eq_131;
    pyc_comb_152 = pyc_eq_132;
    pyc_comb_153 = pyc_zext_134;
    pyc_comb_154 = pyc_add_138;
    pyc_comb_155 = pyc_ult_139;
    pyc_comb_156 = pyc_ult_140;
    pyc_comb_157 = pyc_udiv_141;
    pyc_comb_158 = pyc_ult_142;
    pyc_comb_159 = pyc_ult_143;
    pyc_comb_160 = pyc_mux_148;
  }

  inline void eval_comb_2() {
    pyc_eq_161 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_67) ? 1u : 0u);
    pyc_eq_162 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_65) ? 1u : 0u);
    pyc_eq_163 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_63) ? 1u : 0u);
    pyc_eq_164 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_61) ? 1u : 0u);
    pyc_eq_165 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_59) ? 1u : 0u);
    pyc_eq_166 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_57) ? 1u : 0u);
    pyc_eq_167 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_55) ? 1u : 0u);
    pyc_eq_168 = pyc::cpp::Wire<1>((pyc_comb_160 == pyc_comb_70) ? 1u : 0u);
    pyc_mul_169 = (accum * align_pow10);
    pyc_mux_170 = (pyc_comb_159.toBool() ? pyc_mul_169 : accum);
    pyc_mul_171 = (display_2 * align_pow10);
    pyc_mux_172 = (pyc_comb_158.toBool() ? pyc_mul_171 : display_2);
    pyc_mux_173 = (pyc_comb_158.toBool() ? accum_dp : dp);
    pyc_sub_174 = (pyc_comb_53 - pyc_mux_170);
    pyc_mux_175 = (accum_neg.toBool() ? pyc_sub_174 : pyc_mux_170);
    pyc_sub_176 = (pyc_comb_53 - pyc_mux_172);
    pyc_mux_177 = (neg.toBool() ? pyc_sub_176 : pyc_mux_172);
    pyc_add_178 = (pyc_mux_175 + pyc_mux_177);
    pyc_sub_179 = (pyc_mux_175 - pyc_mux_177);
    pyc_mul_180 = (accum * display_2);
    pyc_xor_181 = (accum_neg ^ neg);
    pyc_zext_182 = pyc::cpp::zext<5, 4>(accum_dp);
    pyc_zext_183 = pyc::cpp::zext<5, 4>(dp);
    pyc_add_184 = (pyc_zext_182 + pyc_zext_183);
    pyc_ult_185 = pyc::cpp::Wire<1>((pyc_comb_52 < pyc_add_184) ? 1u : 0u);
    pyc_sub_186 = (pyc_add_184 - pyc_comb_52);
    pyc_trunc_187 = pyc::cpp::trunc<4, 5>(pyc_sub_186);
    pyc_mux_188 = (pyc_ult_185.toBool() ? pyc_trunc_187 : pyc_comb_51);
    pyc_comb_189 = pyc_eq_161;
    pyc_comb_190 = pyc_eq_162;
    pyc_comb_191 = pyc_eq_163;
    pyc_comb_192 = pyc_eq_164;
    pyc_comb_193 = pyc_eq_165;
    pyc_comb_194 = pyc_eq_166;
    pyc_comb_195 = pyc_eq_167;
    pyc_comb_196 = pyc_eq_168;
    pyc_comb_197 = pyc_mux_173;
    pyc_comb_198 = pyc_add_178;
    pyc_comb_199 = pyc_sub_179;
    pyc_comb_200 = pyc_mul_180;
    pyc_comb_201 = pyc_xor_181;
    pyc_comb_202 = pyc_zext_182;
    pyc_comb_203 = pyc_zext_183;
    pyc_comb_204 = pyc_add_184;
    pyc_comb_205 = pyc_ult_185;
    pyc_comb_206 = pyc_mux_188;
  }

  inline void eval_comb_3() {
    pyc_eq_207 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_67) ? 1u : 0u);
    pyc_eq_208 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_65) ? 1u : 0u);
    pyc_eq_209 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_63) ? 1u : 0u);
    pyc_eq_210 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_61) ? 1u : 0u);
    pyc_eq_211 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_59) ? 1u : 0u);
    pyc_eq_212 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_57) ? 1u : 0u);
    pyc_eq_213 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_55) ? 1u : 0u);
    pyc_eq_214 = pyc::cpp::Wire<1>((pyc_comb_206 == pyc_comb_70) ? 1u : 0u);
    pyc_udiv_215 = pyc::cpp::udiv<64>(pyc_comb_200, trim_pow10);
    pyc_mux_216 = (pyc_comb_205.toBool() ? pyc_udiv_215 : pyc_comb_200);
    pyc_trunc_217 = pyc::cpp::trunc<4, 5>(pyc_comb_204);
    pyc_mux_218 = (pyc_comb_205.toBool() ? pyc_comb_70 : pyc_trunc_217);
    pyc_mul_219 = (accum * pyc_comb_54);
    pyc_eq_220 = pyc::cpp::Wire<1>((display_2 == pyc_comb_53) ? 1u : 0u);
    pyc_mux_221 = (pyc_eq_220.toBool() ? pyc_comb_68 : display_2);
    pyc_udiv_222 = pyc::cpp::udiv<64>(pyc_mul_219, pyc_mux_221);
    pyc_add_223 = (pyc_comb_202 + pyc_comb_52);
    pyc_sub_224 = (pyc_add_223 - pyc_comb_203);
    pyc_ult_225 = pyc::cpp::Wire<1>((pyc_comb_52 < pyc_sub_224) ? 1u : 0u);
    pyc_not_226 = (~pyc_ult_225);
    pyc_trunc_227 = pyc::cpp::trunc<4, 5>(pyc_sub_224);
    pyc_mux_228 = (pyc_not_226.toBool() ? pyc_trunc_227 : pyc_comb_70);
    pyc_comb_229 = pyc_eq_207;
    pyc_comb_230 = pyc_eq_208;
    pyc_comb_231 = pyc_eq_209;
    pyc_comb_232 = pyc_eq_210;
    pyc_comb_233 = pyc_eq_211;
    pyc_comb_234 = pyc_eq_212;
    pyc_comb_235 = pyc_eq_213;
    pyc_comb_236 = pyc_eq_214;
    pyc_comb_237 = pyc_mux_216;
    pyc_comb_238 = pyc_mux_218;
    pyc_comb_239 = pyc_eq_220;
    pyc_comb_240 = pyc_udiv_222;
    pyc_comb_241 = pyc_mux_228;
  }

  inline void eval_comb_4() {
    pyc_slt_242 = pyc::cpp::Wire<1>((pyc::cpp::slt<64>(pyc_comb_198, pyc_comb_53)) ? 1u : 0u);
    pyc_sub_243 = (pyc_comb_53 - pyc_comb_198);
    pyc_mux_244 = (pyc_slt_242.toBool() ? pyc_sub_243 : pyc_comb_198);
    pyc_slt_245 = pyc::cpp::Wire<1>((pyc::cpp::slt<64>(pyc_comb_199, pyc_comb_53)) ? 1u : 0u);
    pyc_sub_246 = (pyc_comb_53 - pyc_comb_199);
    pyc_mux_247 = (pyc_slt_245.toBool() ? pyc_sub_246 : pyc_comb_199);
    pyc_eq_248 = pyc::cpp::Wire<1>((op == pyc_comb_77) ? 1u : 0u);
    pyc_eq_249 = pyc::cpp::Wire<1>((op == pyc_comb_76) ? 1u : 0u);
    pyc_eq_250 = pyc::cpp::Wire<1>((op == pyc_comb_75) ? 1u : 0u);
    pyc_ult_251 = pyc::cpp::Wire<1>((pyc_comb_50 < alu_val) ? 1u : 0u);
    pyc_and_252 = (pyc_comb_239 & pyc_eq_250);
    pyc_or_253 = (pyc_ult_251 | pyc_and_252);
    pyc_udiv_254 = pyc::cpp::udiv<64>(pyc_comb_200, pyc_comb_66);
    pyc_ult_255 = pyc::cpp::Wire<1>((pyc_comb_50 < pyc_udiv_254) ? 1u : 0u);
    pyc_comb_256 = pyc_slt_242;
    pyc_comb_257 = pyc_mux_244;
    pyc_comb_258 = pyc_slt_245;
    pyc_comb_259 = pyc_mux_247;
    pyc_comb_260 = pyc_eq_248;
    pyc_comb_261 = pyc_eq_249;
    pyc_comb_262 = pyc_eq_250;
    pyc_comb_263 = pyc_or_253;
    pyc_comb_264 = pyc_udiv_254;
    pyc_comb_265 = pyc_ult_255;
  }

  inline void eval_comb_5() {
    pyc_and_272 = (pyc_and_270 & pyc_reg_271);
    pyc_not_273 = (~err);
    pyc_comb_274 = pyc_and_272;
    pyc_comb_275 = pyc_not_273;
  }

  inline void eval_comb_6() {
    pyc_and_295 = (pyc_and_293 & pyc_reg_294);
    pyc_not_296 = (~has_dot);
    pyc_comb_297 = pyc_and_295;
    pyc_comb_298 = pyc_not_296;
  }

  inline void eval_comb_7() {
    pyc_or_306 = (pyc_reg_304 | pyc_reg_305);
    pyc_and_307 = (pyc_and_303 & pyc_or_306);
    pyc_comb_308 = pyc_and_307;
  }

  inline void eval_comb_8() {
    pyc_eq_310 = pyc::cpp::Wire<1>((pyc_reg_309 == pyc_comb_48) ? 1u : 0u);
    pyc_and_311 = (pyc_comb_308 & pyc_eq_310);
    pyc_comb_312 = pyc_and_311;
  }

  inline void eval_comb_9() {
    pyc_eq_352 = pyc::cpp::Wire<1>((pyc_reg_351 == pyc_comb_53) ? 1u : 0u);
    pyc_not_353 = (~pyc_eq_352);
    pyc_and_354 = (pyc_and_350 & pyc_not_353);
    pyc_not_355 = (~neg);
    pyc_comb_356 = pyc_and_354;
    pyc_comb_357 = pyc_not_355;
  }

  inline void eval_comb_10() {
    pyc_or_363 = (pyc_reg_361 | pyc_reg_362);
    pyc_and_364 = (pyc_and_360 & pyc_or_363);
    pyc_comb_365 = pyc_and_364;
  }

  inline void eval_comb_11() {
    pyc_ult_367 = pyc::cpp::Wire<1>((pyc_comb_48 < pyc_reg_366) ? 1u : 0u);
    pyc_and_368 = (pyc_comb_365 & pyc_ult_367);
    pyc_comb_369 = pyc_and_368;
  }

  inline void eval_comb_12() {
    pyc_ult_375 = pyc::cpp::Wire<1>((pyc_comb_51 < pyc_reg_374) ? 1u : 0u);
    pyc_and_376 = (pyc_comb_369 & pyc_ult_375);
    pyc_comb_377 = pyc_and_376;
  }

  inline void eval_comb_13() {
    pyc_eq_379 = pyc::cpp::Wire<1>((pyc_reg_378 == pyc_comb_67) ? 1u : 0u);
    pyc_and_380 = (pyc_comb_369 & pyc_eq_379);
    pyc_comb_381 = pyc_and_380;
  }

  inline void eval_comb_14() {
    pyc_eq_383 = pyc::cpp::Wire<1>((pyc_reg_382 == pyc_comb_46) ? 1u : 0u);
    pyc_and_384 = (pyc_comb_369 & pyc_eq_383);
    pyc_comb_385 = pyc_and_384;
  }

  inline void eval_comb_15() {
    pyc_and_390 = (pyc_and_388 & pyc_reg_389);
    pyc_mux_391 = (pyc_comb_124.toBool() ? pyc_comb_53 : display_2);
    pyc_mux_392 = (pyc_and_277.toBool() ? pyc_comb_154 : pyc_mux_391);
    pyc_mux_393 = (pyc_and_290.toBool() ? pyc_comb_153 : pyc_mux_392);
    pyc_mux_394 = (pyc_comb_297.toBool() ? pyc_comb_153 : pyc_mux_393);
    pyc_mux_395 = (pyc_and_317.toBool() ? pyc_comb_53 : pyc_mux_394);
    pyc_mux_396 = (pyc_and_322.toBool() ? pyc_comb_53 : pyc_mux_395);
    pyc_mux_397 = (pyc_and_337.toBool() ? alu_val : pyc_mux_396);
    pyc_mux_398 = (pyc_and_347.toBool() ? alu_val : pyc_mux_397);
    pyc_mux_399 = (pyc_comb_369.toBool() ? pyc_comb_157 : pyc_mux_398);
    pyc_mux_400 = (pyc_and_390.toBool() ? pyc_comb_264 : pyc_mux_399);
    pyc_comb_401 = pyc_and_390;
    pyc_comb_402 = pyc_mux_400;
  }

  inline void eval_comb_16() {
    pyc_mux_404 = (pyc_comb_124.toBool() ? pyc_comb_53 : accum);
    pyc_mux_405 = (pyc_comb_297.toBool() ? pyc_comb_53 : pyc_mux_404);
    pyc_mux_406 = (pyc_and_322.toBool() ? pyc_comb_53 : pyc_mux_405);
    pyc_mux_407 = (pyc_and_327.toBool() ? display_2 : pyc_mux_406);
    pyc_mux_408 = (pyc_and_337.toBool() ? alu_val : pyc_mux_407);
    pyc_mux_409 = (pyc_and_342.toBool() ? display_2 : pyc_mux_408);
    pyc_comb_410 = pyc_mux_409;
  }

  inline void eval_comb_17() {
    pyc_mux_412 = (pyc_comb_124.toBool() ? pyc_comb_74 : state);
    pyc_mux_413 = (pyc_and_290.toBool() ? pyc_comb_72 : pyc_mux_412);
    pyc_mux_414 = (pyc_comb_297.toBool() ? pyc_comb_74 : pyc_mux_413);
    pyc_mux_415 = (pyc_and_317.toBool() ? pyc_comb_72 : pyc_mux_414);
    pyc_mux_416 = (pyc_and_322.toBool() ? pyc_comb_74 : pyc_mux_415);
    pyc_mux_417 = (pyc_and_327.toBool() ? pyc_comb_73 : pyc_mux_416);
    pyc_mux_418 = (pyc_and_337.toBool() ? pyc_comb_73 : pyc_mux_417);
    pyc_mux_419 = (pyc_and_342.toBool() ? pyc_comb_73 : pyc_mux_418);
    pyc_mux_420 = (pyc_and_347.toBool() ? pyc_comb_71 : pyc_mux_419);
    pyc_comb_421 = pyc_mux_420;
  }

  inline void eval_comb_18() {
    pyc_mux_423 = (pyc_comb_124.toBool() ? pyc_comb_79 : op);
    pyc_mux_424 = (pyc_comb_297.toBool() ? pyc_comb_79 : pyc_mux_423);
    pyc_mux_425 = (pyc_and_322.toBool() ? pyc_comb_79 : pyc_mux_424);
    pyc_mux_426 = (pyc_and_327.toBool() ? new_op : pyc_mux_425);
    pyc_mux_427 = (pyc_and_332.toBool() ? new_op : pyc_mux_426);
    pyc_mux_428 = (pyc_and_337.toBool() ? new_op : pyc_mux_427);
    pyc_mux_429 = (pyc_and_342.toBool() ? new_op : pyc_mux_428);
    pyc_comb_430 = pyc_mux_429;
  }

  inline void eval_comb_19() {
    pyc_mux_432 = (pyc_comb_124.toBool() ? pyc_comb_49 : neg);
    pyc_mux_433 = (pyc_and_290.toBool() ? pyc_comb_49 : pyc_mux_432);
    pyc_mux_434 = (pyc_comb_297.toBool() ? pyc_comb_49 : pyc_mux_433);
    pyc_mux_435 = (pyc_and_317.toBool() ? pyc_comb_49 : pyc_mux_434);
    pyc_mux_436 = (pyc_and_322.toBool() ? pyc_comb_49 : pyc_mux_435);
    pyc_mux_437 = (pyc_and_337.toBool() ? alu_neg : pyc_mux_436);
    pyc_mux_438 = (pyc_and_347.toBool() ? alu_neg : pyc_mux_437);
    pyc_mux_439 = (pyc_comb_356.toBool() ? pyc_comb_357 : pyc_mux_438);
    pyc_mux_440 = (pyc_comb_385.toBool() ? pyc_comb_49 : pyc_mux_439);
    pyc_mux_441 = (pyc_comb_401.toBool() ? pyc_comb_49 : pyc_mux_440);
    pyc_comb_442 = pyc_mux_441;
  }

  inline void eval_comb_20() {
    pyc_mux_444 = (pyc_comb_124.toBool() ? pyc_comb_49 : err);
    pyc_mux_445 = (pyc_and_337.toBool() ? pyc_comb_263 : pyc_mux_444);
    pyc_mux_446 = (pyc_and_347.toBool() ? pyc_comb_263 : pyc_mux_445);
    pyc_mux_447 = (pyc_comb_401.toBool() ? pyc_comb_265 : pyc_mux_446);
    pyc_comb_448 = pyc_mux_447;
  }

  inline void eval_comb_21() {
    pyc_constant_1 = pyc::cpp::Wire<5>({0x1ull});
    pyc_constant_2 = pyc::cpp::Wire<1>({0x1ull});
    pyc_constant_3 = pyc::cpp::Wire<5>({0x0ull});
    pyc_constant_4 = pyc::cpp::Wire<1>({0x0ull});
    pyc_constant_5 = pyc::cpp::Wire<64>({0x2386F26FC0FFFFull});
    pyc_constant_6 = pyc::cpp::Wire<4>({0x0ull});
    pyc_constant_7 = pyc::cpp::Wire<5>({0x8ull});
    pyc_constant_8 = pyc::cpp::Wire<64>({0x0ull});
    pyc_constant_9 = pyc::cpp::Wire<64>({0x5F5E100ull});
    pyc_constant_10 = pyc::cpp::Wire<4>({0x7ull});
    pyc_constant_11 = pyc::cpp::Wire<64>({0x989680ull});
    pyc_constant_12 = pyc::cpp::Wire<4>({0x6ull});
    pyc_constant_13 = pyc::cpp::Wire<64>({0xF4240ull});
    pyc_constant_14 = pyc::cpp::Wire<4>({0x5ull});
    pyc_constant_15 = pyc::cpp::Wire<64>({0x186A0ull});
    pyc_constant_16 = pyc::cpp::Wire<4>({0x4ull});
    pyc_constant_17 = pyc::cpp::Wire<64>({0x2710ull});
    pyc_constant_18 = pyc::cpp::Wire<4>({0x3ull});
    pyc_constant_19 = pyc::cpp::Wire<64>({0x3E8ull});
    pyc_constant_20 = pyc::cpp::Wire<4>({0x2ull});
    pyc_constant_21 = pyc::cpp::Wire<64>({0x64ull});
    pyc_constant_22 = pyc::cpp::Wire<4>({0x1ull});
    pyc_constant_23 = pyc::cpp::Wire<64>({0x1ull});
    pyc_constant_24 = pyc::cpp::Wire<64>({0xAull});
    pyc_constant_25 = pyc::cpp::Wire<4>({0x8ull});
    pyc_constant_26 = pyc::cpp::Wire<2>({0x3ull});
    pyc_constant_27 = pyc::cpp::Wire<2>({0x2ull});
    pyc_constant_28 = pyc::cpp::Wire<2>({0x1ull});
    pyc_constant_29 = pyc::cpp::Wire<2>({0x0ull});
    pyc_constant_30 = pyc::cpp::Wire<3>({0x4ull});
    pyc_constant_31 = pyc::cpp::Wire<3>({0x3ull});
    pyc_constant_32 = pyc::cpp::Wire<3>({0x2ull});
    pyc_constant_33 = pyc::cpp::Wire<3>({0x1ull});
    pyc_constant_34 = pyc::cpp::Wire<3>({0x0ull});
    pyc_constant_35 = pyc::cpp::Wire<5>({0x13ull});
    pyc_constant_36 = pyc::cpp::Wire<5>({0x12ull});
    pyc_constant_37 = pyc::cpp::Wire<5>({0x11ull});
    pyc_constant_38 = pyc::cpp::Wire<5>({0x10ull});
    pyc_constant_39 = pyc::cpp::Wire<5>({0xFull});
    pyc_constant_40 = pyc::cpp::Wire<5>({0xEull});
    pyc_constant_41 = pyc::cpp::Wire<5>({0xDull});
    pyc_constant_42 = pyc::cpp::Wire<5>({0xCull});
    pyc_constant_43 = pyc::cpp::Wire<5>({0xBull});
    pyc_constant_44 = pyc::cpp::Wire<5>({0xAull});
    pyc_constant_45 = pyc::cpp::Wire<5>({0x9ull});
    pyc_comb_46 = pyc_constant_1;
    pyc_comb_47 = pyc_constant_2;
    pyc_comb_48 = pyc_constant_3;
    pyc_comb_49 = pyc_constant_4;
    pyc_comb_50 = pyc_constant_5;
    pyc_comb_51 = pyc_constant_6;
    pyc_comb_52 = pyc_constant_7;
    pyc_comb_53 = pyc_constant_8;
    pyc_comb_54 = pyc_constant_9;
    pyc_comb_55 = pyc_constant_10;
    pyc_comb_56 = pyc_constant_11;
    pyc_comb_57 = pyc_constant_12;
    pyc_comb_58 = pyc_constant_13;
    pyc_comb_59 = pyc_constant_14;
    pyc_comb_60 = pyc_constant_15;
    pyc_comb_61 = pyc_constant_16;
    pyc_comb_62 = pyc_constant_17;
    pyc_comb_63 = pyc_constant_18;
    pyc_comb_64 = pyc_constant_19;
    pyc_comb_65 = pyc_constant_20;
    pyc_comb_66 = pyc_constant_21;
    pyc_comb_67 = pyc_constant_22;
    pyc_comb_68 = pyc_constant_23;
    pyc_comb_69 = pyc_constant_24;
    pyc_comb_70 = pyc_constant_25;
    pyc_comb_71 = pyc_constant_26;
    pyc_comb_72 = pyc_constant_27;
    pyc_comb_73 = pyc_constant_28;
    pyc_comb_74 = pyc_constant_29;
    pyc_comb_75 = pyc_constant_30;
    pyc_comb_76 = pyc_constant_31;
    pyc_comb_77 = pyc_constant_32;
    pyc_comb_78 = pyc_constant_33;
    pyc_comb_79 = pyc_constant_34;
    pyc_comb_80 = pyc_constant_35;
    pyc_comb_81 = pyc_constant_36;
    pyc_comb_82 = pyc_constant_37;
    pyc_comb_83 = pyc_constant_38;
    pyc_comb_84 = pyc_constant_39;
    pyc_comb_85 = pyc_constant_40;
    pyc_comb_86 = pyc_constant_41;
    pyc_comb_87 = pyc_constant_42;
    pyc_comb_88 = pyc_constant_43;
    pyc_comb_89 = pyc_constant_44;
    pyc_comb_90 = pyc_constant_45;
  }

  inline void eval_comb_22() {
    pyc_mux_450 = (pyc_comb_124.toBool() ? pyc_comb_48 : digit_cnt);
    pyc_mux_451 = (pyc_and_277.toBool() ? pyc_add_279 : pyc_mux_450);
    pyc_mux_452 = (pyc_and_290.toBool() ? pyc_comb_46 : pyc_mux_451);
    pyc_mux_453 = (pyc_comb_297.toBool() ? pyc_comb_46 : pyc_mux_452);
    pyc_mux_454 = (pyc_comb_312.toBool() ? pyc_comb_46 : pyc_mux_453);
    pyc_mux_455 = (pyc_and_317.toBool() ? pyc_comb_46 : pyc_mux_454);
    pyc_mux_456 = (pyc_and_322.toBool() ? pyc_comb_46 : pyc_mux_455);
    pyc_mux_457 = (pyc_and_337.toBool() ? pyc_comb_48 : pyc_mux_456);
    pyc_mux_458 = (pyc_and_347.toBool() ? pyc_comb_48 : pyc_mux_457);
    pyc_mux_459 = (pyc_comb_369.toBool() ? pyc_sub_371 : pyc_mux_458);
    pyc_mux_460 = (pyc_comb_401.toBool() ? pyc_comb_48 : pyc_mux_459);
    pyc_comb_461 = pyc_mux_460;
  }

  inline void eval_comb_23() {
    pyc_mux_463 = (pyc_comb_124.toBool() ? pyc_comb_49 : accum_neg);
    pyc_mux_464 = (pyc_comb_297.toBool() ? pyc_comb_49 : pyc_mux_463);
    pyc_mux_465 = (pyc_and_322.toBool() ? pyc_comb_49 : pyc_mux_464);
    pyc_mux_466 = (pyc_and_327.toBool() ? neg : pyc_mux_465);
    pyc_mux_467 = (pyc_and_337.toBool() ? alu_neg : pyc_mux_466);
    pyc_mux_468 = (pyc_and_342.toBool() ? neg : pyc_mux_467);
    pyc_comb_469 = pyc_mux_468;
  }

  inline void eval_comb_24() {
    pyc_mux_471 = (pyc_comb_124.toBool() ? pyc_comb_51 : dp);
    pyc_mux_472 = (pyc_and_285.toBool() ? pyc_add_281 : pyc_mux_471);
    pyc_mux_473 = (pyc_and_290.toBool() ? pyc_comb_51 : pyc_mux_472);
    pyc_mux_474 = (pyc_comb_297.toBool() ? pyc_comb_51 : pyc_mux_473);
    pyc_mux_475 = (pyc_and_317.toBool() ? pyc_comb_51 : pyc_mux_474);
    pyc_mux_476 = (pyc_and_322.toBool() ? pyc_comb_51 : pyc_mux_475);
    pyc_mux_477 = (pyc_and_337.toBool() ? alu_dp : pyc_mux_476);
    pyc_mux_478 = (pyc_and_347.toBool() ? alu_dp : pyc_mux_477);
    pyc_mux_479 = (pyc_comb_377.toBool() ? pyc_sub_373 : pyc_mux_478);
    pyc_mux_480 = (pyc_comb_401.toBool() ? pyc_comb_238 : pyc_mux_479);
    pyc_comb_481 = pyc_mux_480;
  }

  inline void eval_comb_25() {
    pyc_mux_483 = (pyc_comb_124.toBool() ? pyc_comb_51 : accum_dp);
    pyc_mux_484 = (pyc_comb_297.toBool() ? pyc_comb_51 : pyc_mux_483);
    pyc_mux_485 = (pyc_and_322.toBool() ? pyc_comb_51 : pyc_mux_484);
    pyc_mux_486 = (pyc_and_327.toBool() ? dp : pyc_mux_485);
    pyc_mux_487 = (pyc_and_337.toBool() ? alu_dp : pyc_mux_486);
    pyc_mux_488 = (pyc_and_342.toBool() ? dp : pyc_mux_487);
    pyc_comb_489 = pyc_mux_488;
  }

  inline void eval_comb_26() {
    pyc_mux_491 = (pyc_comb_124.toBool() ? pyc_comb_49 : has_dot);
    pyc_mux_492 = (pyc_and_290.toBool() ? pyc_comb_49 : pyc_mux_491);
    pyc_mux_493 = (pyc_comb_297.toBool() ? pyc_comb_49 : pyc_mux_492);
    pyc_mux_494 = (pyc_comb_308.toBool() ? pyc_comb_47 : pyc_mux_493);
    pyc_mux_495 = (pyc_and_317.toBool() ? pyc_comb_47 : pyc_mux_494);
    pyc_mux_496 = (pyc_and_322.toBool() ? pyc_comb_47 : pyc_mux_495);
    pyc_mux_497 = (pyc_and_327.toBool() ? pyc_comb_49 : pyc_mux_496);
    pyc_mux_498 = (pyc_and_337.toBool() ? pyc_comb_49 : pyc_mux_497);
    pyc_mux_499 = (pyc_and_342.toBool() ? pyc_comb_49 : pyc_mux_498);
    pyc_mux_500 = (pyc_and_347.toBool() ? pyc_comb_49 : pyc_mux_499);
    pyc_mux_501 = (pyc_comb_381.toBool() ? pyc_comb_49 : pyc_mux_500);
    pyc_mux_502 = (pyc_comb_401.toBool() ? pyc_comb_49 : pyc_mux_501);
    pyc_comb_503 = pyc_mux_502;
  }

  inline void eval_comb_27() {
    pyc_mux_505 = (pyc_comb_118.toBool() ? pyc_comb_78 : pyc_comb_79);
    pyc_mux_506 = (pyc_comb_119.toBool() ? pyc_comb_77 : pyc_mux_505);
    pyc_mux_507 = (pyc_comb_120.toBool() ? pyc_comb_76 : pyc_mux_506);
    pyc_mux_508 = (pyc_comb_121.toBool() ? pyc_comb_75 : pyc_mux_507);
    pyc_comb_509 = pyc_mux_508;
  }

  inline void eval_comb_28() {
    pyc_mux_510 = (pyc_comb_189.toBool() ? pyc_comb_69 : pyc_comb_68);
    pyc_mux_511 = (pyc_comb_190.toBool() ? pyc_comb_66 : pyc_mux_510);
    pyc_mux_512 = (pyc_comb_191.toBool() ? pyc_comb_64 : pyc_mux_511);
    pyc_mux_513 = (pyc_comb_192.toBool() ? pyc_comb_62 : pyc_mux_512);
    pyc_mux_514 = (pyc_comb_193.toBool() ? pyc_comb_60 : pyc_mux_513);
    pyc_mux_515 = (pyc_comb_194.toBool() ? pyc_comb_58 : pyc_mux_514);
    pyc_mux_516 = (pyc_comb_195.toBool() ? pyc_comb_56 : pyc_mux_515);
    pyc_mux_517 = (pyc_comb_196.toBool() ? pyc_comb_54 : pyc_mux_516);
    pyc_comb_518 = pyc_mux_517;
  }

  inline void eval_comb_29() {
    pyc_mux_519 = (pyc_comb_229.toBool() ? pyc_comb_69 : pyc_comb_68);
    pyc_mux_520 = (pyc_comb_230.toBool() ? pyc_comb_66 : pyc_mux_519);
    pyc_mux_521 = (pyc_comb_231.toBool() ? pyc_comb_64 : pyc_mux_520);
    pyc_mux_522 = (pyc_comb_232.toBool() ? pyc_comb_62 : pyc_mux_521);
    pyc_mux_523 = (pyc_comb_233.toBool() ? pyc_comb_60 : pyc_mux_522);
    pyc_mux_524 = (pyc_comb_234.toBool() ? pyc_comb_58 : pyc_mux_523);
    pyc_mux_525 = (pyc_comb_235.toBool() ? pyc_comb_56 : pyc_mux_524);
    pyc_mux_526 = (pyc_comb_236.toBool() ? pyc_comb_54 : pyc_mux_525);
    pyc_comb_527 = pyc_mux_526;
  }

  inline void eval_comb_30() {
    pyc_mux_528 = (pyc_comb_260.toBool() ? pyc_comb_259 : pyc_comb_257);
    pyc_mux_529 = (pyc_comb_261.toBool() ? pyc_comb_237 : pyc_mux_528);
    pyc_mux_530 = (pyc_comb_262.toBool() ? pyc_comb_240 : pyc_mux_529);
    pyc_comb_531 = pyc_mux_530;
  }

  inline void eval_comb_31() {
    pyc_mux_532 = (pyc_comb_260.toBool() ? pyc_comb_258 : pyc_comb_256);
    pyc_mux_533 = (pyc_comb_261.toBool() ? pyc_comb_201 : pyc_mux_532);
    pyc_mux_534 = (pyc_comb_262.toBool() ? pyc_comb_201 : pyc_mux_533);
    pyc_comb_535 = pyc_mux_534;
  }

  inline void eval_comb_32() {
    pyc_mux_536 = (pyc_comb_261.toBool() ? pyc_comb_238 : pyc_comb_197);
    pyc_mux_537 = (pyc_comb_262.toBool() ? pyc_comb_241 : pyc_mux_536);
    pyc_comb_538 = pyc_mux_537;
  }

  inline void eval_comb_pass() {
    eval_comb_21();
    eval_comb_0();
    eval_comb_1();
    eval_comb_2();
    eval_comb_3();
    eval_comb_4();
    pyc_or_268 = (pyc_reg_266 | pyc_reg_267);
    pyc_and_270 = (pyc_reg_269 & pyc_or_268);
    eval_comb_5();
    pyc_and_277 = (pyc_comb_274 & pyc_reg_276);
    pyc_add_279 = (pyc_reg_278 + pyc_comb_46);
    pyc_add_281 = (pyc_reg_280 + pyc_comb_67);
    pyc_and_283 = (pyc_and_277 & pyc_reg_282);
    pyc_and_285 = (pyc_and_283 & pyc_reg_284);
    pyc_and_288 = (pyc_reg_286 & pyc_reg_287);
    pyc_and_290 = (pyc_and_288 & pyc_reg_289);
    pyc_and_293 = (pyc_reg_291 & pyc_reg_292);
    eval_comb_6();
    pyc_and_301 = (pyc_reg_299 & pyc_reg_300);
    pyc_and_303 = (pyc_and_301 & pyc_reg_302);
    eval_comb_7();
    eval_comb_8();
    pyc_and_315 = (pyc_reg_313 & pyc_reg_314);
    pyc_and_317 = (pyc_and_315 & pyc_reg_316);
    pyc_and_320 = (pyc_reg_318 & pyc_reg_319);
    pyc_and_322 = (pyc_and_320 & pyc_reg_321);
    pyc_and_325 = (pyc_reg_323 & pyc_reg_324);
    pyc_and_327 = (pyc_and_325 & pyc_reg_326);
    pyc_and_330 = (pyc_reg_328 & pyc_reg_329);
    pyc_and_332 = (pyc_and_330 & pyc_reg_331);
    pyc_and_335 = (pyc_reg_333 & pyc_reg_334);
    pyc_and_337 = (pyc_and_335 & pyc_reg_336);
    pyc_and_340 = (pyc_reg_338 & pyc_reg_339);
    pyc_and_342 = (pyc_and_340 & pyc_reg_341);
    pyc_and_345 = (pyc_reg_343 & pyc_reg_344);
    pyc_and_347 = (pyc_and_345 & pyc_reg_346);
    pyc_and_350 = (pyc_reg_348 & pyc_reg_349);
    eval_comb_9();
    pyc_and_360 = (pyc_reg_358 & pyc_reg_359);
    eval_comb_10();
    eval_comb_11();
    pyc_sub_371 = (pyc_reg_370 - pyc_comb_46);
    pyc_sub_373 = (pyc_reg_372 - pyc_comb_67);
    eval_comb_12();
    eval_comb_13();
    eval_comb_14();
    pyc_and_388 = (pyc_reg_386 & pyc_reg_387);
    eval_comb_15();
    display_2 = pyc_reg_403;
    eval_comb_16();
    accum = pyc_reg_411;
    eval_comb_17();
    state = pyc_reg_422;
    eval_comb_18();
    op = pyc_reg_431;
    eval_comb_19();
    neg = pyc_reg_443;
    eval_comb_20();
    err = pyc_reg_449;
    eval_comb_22();
    digit_cnt = pyc_reg_462;
    eval_comb_23();
    accum_neg = pyc_reg_470;
    eval_comb_24();
    dp = pyc_reg_482;
    eval_comb_25();
    accum_dp = pyc_reg_490;
    eval_comb_26();
    has_dot = pyc_reg_504;
    eval_comb_27();
    new_op = pyc_comb_509;
    eval_comb_28();
    align_pow10 = pyc_comb_518;
    eval_comb_29();
    trim_pow10 = pyc_comb_527;
    eval_comb_30();
    alu_val = pyc_comb_531;
    eval_comb_31();
    alu_neg = pyc_comb_535;
    eval_comb_32();
    alu_dp = pyc_comb_538;
  }

  void eval() {
    eval_comb_pass();
    display = display_2;
    display_neg = neg;
    display_err = err;
    display_dp = dp;
    op_pending = op;
  }

  void tick() {
    // Two-phase update: compute next state for all sequential elements,
    // then commit together. This avoids ordering artifacts between regs.
    // Phase 1: compute.
    pyc_reg_266_inst.tick_compute();
    pyc_reg_267_inst.tick_compute();
    pyc_reg_269_inst.tick_compute();
    pyc_reg_271_inst.tick_compute();
    pyc_reg_276_inst.tick_compute();
    pyc_reg_278_inst.tick_compute();
    pyc_reg_280_inst.tick_compute();
    pyc_reg_282_inst.tick_compute();
    pyc_reg_284_inst.tick_compute();
    pyc_reg_286_inst.tick_compute();
    pyc_reg_287_inst.tick_compute();
    pyc_reg_289_inst.tick_compute();
    pyc_reg_291_inst.tick_compute();
    pyc_reg_292_inst.tick_compute();
    pyc_reg_294_inst.tick_compute();
    pyc_reg_299_inst.tick_compute();
    pyc_reg_300_inst.tick_compute();
    pyc_reg_302_inst.tick_compute();
    pyc_reg_304_inst.tick_compute();
    pyc_reg_305_inst.tick_compute();
    pyc_reg_309_inst.tick_compute();
    pyc_reg_313_inst.tick_compute();
    pyc_reg_314_inst.tick_compute();
    pyc_reg_316_inst.tick_compute();
    pyc_reg_318_inst.tick_compute();
    pyc_reg_319_inst.tick_compute();
    pyc_reg_321_inst.tick_compute();
    pyc_reg_323_inst.tick_compute();
    pyc_reg_324_inst.tick_compute();
    pyc_reg_326_inst.tick_compute();
    pyc_reg_328_inst.tick_compute();
    pyc_reg_329_inst.tick_compute();
    pyc_reg_331_inst.tick_compute();
    pyc_reg_333_inst.tick_compute();
    pyc_reg_334_inst.tick_compute();
    pyc_reg_336_inst.tick_compute();
    pyc_reg_338_inst.tick_compute();
    pyc_reg_339_inst.tick_compute();
    pyc_reg_341_inst.tick_compute();
    pyc_reg_343_inst.tick_compute();
    pyc_reg_344_inst.tick_compute();
    pyc_reg_346_inst.tick_compute();
    pyc_reg_348_inst.tick_compute();
    pyc_reg_349_inst.tick_compute();
    pyc_reg_351_inst.tick_compute();
    pyc_reg_358_inst.tick_compute();
    pyc_reg_359_inst.tick_compute();
    pyc_reg_361_inst.tick_compute();
    pyc_reg_362_inst.tick_compute();
    pyc_reg_366_inst.tick_compute();
    pyc_reg_370_inst.tick_compute();
    pyc_reg_372_inst.tick_compute();
    pyc_reg_374_inst.tick_compute();
    pyc_reg_378_inst.tick_compute();
    pyc_reg_382_inst.tick_compute();
    pyc_reg_386_inst.tick_compute();
    pyc_reg_387_inst.tick_compute();
    pyc_reg_389_inst.tick_compute();
    pyc_reg_403_inst.tick_compute();
    pyc_reg_411_inst.tick_compute();
    pyc_reg_422_inst.tick_compute();
    pyc_reg_431_inst.tick_compute();
    pyc_reg_443_inst.tick_compute();
    pyc_reg_449_inst.tick_compute();
    pyc_reg_462_inst.tick_compute();
    pyc_reg_470_inst.tick_compute();
    pyc_reg_482_inst.tick_compute();
    pyc_reg_490_inst.tick_compute();
    pyc_reg_504_inst.tick_compute();
    // Phase 2: commit.
    pyc_reg_266_inst.tick_commit();
    pyc_reg_267_inst.tick_commit();
    pyc_reg_269_inst.tick_commit();
    pyc_reg_271_inst.tick_commit();
    pyc_reg_276_inst.tick_commit();
    pyc_reg_278_inst.tick_commit();
    pyc_reg_280_inst.tick_commit();
    pyc_reg_282_inst.tick_commit();
    pyc_reg_284_inst.tick_commit();
    pyc_reg_286_inst.tick_commit();
    pyc_reg_287_inst.tick_commit();
    pyc_reg_289_inst.tick_commit();
    pyc_reg_291_inst.tick_commit();
    pyc_reg_292_inst.tick_commit();
    pyc_reg_294_inst.tick_commit();
    pyc_reg_299_inst.tick_commit();
    pyc_reg_300_inst.tick_commit();
    pyc_reg_302_inst.tick_commit();
    pyc_reg_304_inst.tick_commit();
    pyc_reg_305_inst.tick_commit();
    pyc_reg_309_inst.tick_commit();
    pyc_reg_313_inst.tick_commit();
    pyc_reg_314_inst.tick_commit();
    pyc_reg_316_inst.tick_commit();
    pyc_reg_318_inst.tick_commit();
    pyc_reg_319_inst.tick_commit();
    pyc_reg_321_inst.tick_commit();
    pyc_reg_323_inst.tick_commit();
    pyc_reg_324_inst.tick_commit();
    pyc_reg_326_inst.tick_commit();
    pyc_reg_328_inst.tick_commit();
    pyc_reg_329_inst.tick_commit();
    pyc_reg_331_inst.tick_commit();
    pyc_reg_333_inst.tick_commit();
    pyc_reg_334_inst.tick_commit();
    pyc_reg_336_inst.tick_commit();
    pyc_reg_338_inst.tick_commit();
    pyc_reg_339_inst.tick_commit();
    pyc_reg_341_inst.tick_commit();
    pyc_reg_343_inst.tick_commit();
    pyc_reg_344_inst.tick_commit();
    pyc_reg_346_inst.tick_commit();
    pyc_reg_348_inst.tick_commit();
    pyc_reg_349_inst.tick_commit();
    pyc_reg_351_inst.tick_commit();
    pyc_reg_358_inst.tick_commit();
    pyc_reg_359_inst.tick_commit();
    pyc_reg_361_inst.tick_commit();
    pyc_reg_362_inst.tick_commit();
    pyc_reg_366_inst.tick_commit();
    pyc_reg_370_inst.tick_commit();
    pyc_reg_372_inst.tick_commit();
    pyc_reg_374_inst.tick_commit();
    pyc_reg_378_inst.tick_commit();
    pyc_reg_382_inst.tick_commit();
    pyc_reg_386_inst.tick_commit();
    pyc_reg_387_inst.tick_commit();
    pyc_reg_389_inst.tick_commit();
    pyc_reg_403_inst.tick_commit();
    pyc_reg_411_inst.tick_commit();
    pyc_reg_422_inst.tick_commit();
    pyc_reg_431_inst.tick_commit();
    pyc_reg_443_inst.tick_commit();
    pyc_reg_449_inst.tick_commit();
    pyc_reg_462_inst.tick_commit();
    pyc_reg_470_inst.tick_commit();
    pyc_reg_482_inst.tick_commit();
    pyc_reg_490_inst.tick_commit();
    pyc_reg_504_inst.tick_commit();
  }
};

} // namespace pyc::gen
