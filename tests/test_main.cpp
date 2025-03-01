#ifdef _GLIBCXX_DEBUG
// Workaround to allow randomly ordered tests.  See
// https://github.com/catchorg/Catch2/issues/1384
// https://stackoverflow.com/questions/22915325/avoiding-self-assignment-in-stdshuffle/23691322
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85828
#include <iosfwd> // Any cheap-to-include stdlib header
#ifdef __GLIBCXX__
#include <debug/macros.h> // IWYU pragma: keep

#undef __glibcxx_check_self_move_assign
#define __glibcxx_check_self_move_assign(x)
#endif // __GLIBCXX__
#endif // _GLIBCXX_DEBUG

#ifdef CATA_CATCH_PCH
#undef TWOBLUECUBES_SINGLE_INCLUDE_CATCH_HPP_INCLUDED
#define CATCH_CONFIG_IMPL_ONLY
#endif
#define CATCH_CONFIG_RUNNER
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "color.h"
#include "debug.h"
#include "distribution_grid.h"
#include "filesystem.h"
#include "game.h"
#include "init.h"
#include "language.h"
#include "loading_ui.h"
#include "map.h"
#include "mod_manager.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_special.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "pldata.h"
#include "point.h"
#include "rng.h"
#include "string_utils.h"
#include "string_formatter.h"
#include "type_id.h"
#include "weather.h"
#include "worldfactory.h"

using name_value_pair_t = std::pair<std::string, std::string>;
using option_overrides_t = std::vector<name_value_pair_t>;

// If tag is found as a prefix of any argument in arg_vec, the argument is
// removed from arg_vec and the argument suffix after tag is returned.
// Otherwise, an empty string is returned and arg_vec is unchanged.
static std::string extract_argument( std::vector<const char *> &arg_vec, const std::string &tag )
{
    std::string arg_rest;
    for( auto iter = arg_vec.begin(); iter != arg_vec.end(); iter++ ) {
        if( strncmp( *iter, tag.c_str(), tag.length() ) == 0 ) {
            arg_rest = std::string( &( *iter )[tag.length()] );
            arg_vec.erase( iter );
            break;
        }
    }
    return arg_rest;
}

static std::vector<mod_id> extract_mod_selection( std::vector<const char *> &arg_vec )
{
    std::string mod_string = extract_argument( arg_vec, "--mods=" );

    std::vector<std::string> mod_names = string_split( mod_string, ',' );
    std::vector<mod_id> ret;
    for( const std::string &mod_name : mod_names ) {
        if( !mod_name.empty() ) {
            ret.emplace_back( mod_name );
        }
    }
    // Always load test data mod
    ret.emplace_back( "test_data" );

    return ret;
}

static void init_global_game_state( const std::vector<mod_id> &mods,
                                    option_overrides_t &option_overrides,
                                    const std::string &user_dir )
{
    if( !assure_dir_exist( user_dir ) ) {
        assert( !"Unable to make user_dir directory.  Check permissions." );
    }

    PATH_INFO::init_base_path( "" );
    PATH_INFO::init_user_dir( user_dir );
    PATH_INFO::set_standard_filenames();

    if( !assure_dir_exist( PATH_INFO::config_dir() ) ) {
        assert( !"Unable to make config directory.  Check permissions." );
    }

    if( !assure_dir_exist( PATH_INFO::savedir() ) ) {
        assert( !"Unable to make save directory.  Check permissions." );
    }

    if( !assure_dir_exist( PATH_INFO::templatedir() ) ) {
        assert( !"Unable to make templates directory.  Check permissions." );
    }

    if( !init_language_system() ) {
        DebugLog( DL::Error, DC::Main ) << "Failed to init language system.";
    }

    get_options().init();
    get_options().load();

    // Apply command-line option overrides for test suite execution.
    if( !option_overrides.empty() ) {
        for( const name_value_pair_t &option : option_overrides ) {
            if( get_options().has_option( option.first ) ) {
                options_manager::cOpt &opt = get_options().get_option( option.first );
                opt.setValue( option.second );
            }
        }
    }
    init_colors();

    g = std::make_unique<game>( );
    g->new_game = true;
    g->load_static_data();

    world_generator->set_active_world( nullptr );
    world_generator->init();
    WORLDPTR test_world = world_generator->make_new_world( mods );
    assert( test_world != nullptr );
    world_generator->set_active_world( test_world );
    assert( world_generator->active_world != nullptr );

    calendar::set_eternal_season( get_option<bool>( "ETERNAL_SEASON" ) );
    calendar::set_season_length( get_option<int>( "SEASON_LENGTH" ) );

    loading_ui ui( false );
    init::load_world_modfiles( ui, g->get_world_base_save_path() + "/" + SAVE_ARTIFACTS );

    g->u = avatar();
    g->u.create( character_type::NOW );

    g->m = map( get_option<bool>( "ZLEVELS" ) );
    disable_mapgen = true;

    g->m.load( tripoint( g->get_levx(), g->get_levy(), g->get_levz() ), false );
    get_distribution_grid_tracker().load( g->m );

    get_weather().update_weather();
}

// Checks if any of the flags are in container, removes them all
static bool check_remove_flags( std::vector<const char *> &cont,
                                const std::vector<const char *> &flags )
{
    bool has_any = false;
    auto iter = flags.begin();
    while( iter != flags.end() ) {
        auto found = std::find_if( cont.begin(), cont.end(),
        [iter]( const char *c ) {
            return strcmp( c, *iter ) == 0;
        } );
        if( found == cont.end() ) {
            iter++;
        } else {
            cont.erase( found );
            has_any = true;
        }
    }

    return has_any;
}

// Split s on separator sep, returning parts as a pair. Returns empty string as
// second value if no separator found.
static name_value_pair_t split_pair( const std::string &s, const char sep )
{
    const size_t pos = s.find( sep );
    if( pos != std::string::npos ) {
        return name_value_pair_t( s.substr( 0, pos ), s.substr( pos + 1 ) );
    } else {
        return name_value_pair_t( s, "" );
    }
}

static option_overrides_t extract_option_overrides( std::vector<const char *> &arg_vec )
{
    option_overrides_t ret;
    std::string option_overrides_string = extract_argument( arg_vec, "--option_overrides=" );
    if( option_overrides_string.empty() ) {
        return ret;
    }
    const char delim = ',';
    const char sep = ':';
    size_t i = 0;
    size_t pos = option_overrides_string.find( delim );
    while( pos != std::string::npos ) {
        std::string part = option_overrides_string.substr( i, pos );
        ret.emplace_back( split_pair( part, sep ) );
        i = ++pos;
        pos = option_overrides_string.find( delim, pos );
    }
    // Handle last part
    const std::string part = option_overrides_string.substr( i );
    ret.emplace_back( split_pair( part, sep ) );
    return ret;
}

static std::string extract_user_dir( std::vector<const char *> &arg_vec )
{
    std::string option_user_dir = extract_argument( arg_vec, "--user-dir=" );
    if( option_user_dir.empty() ) {
        return "./test_user_dir/";
    }
    if( !string_ends_with( option_user_dir, "/" ) ) {
        option_user_dir += "/";
    }
    return option_user_dir;
}

struct CataListener : Catch::TestEventListenerBase {
    using TestEventListenerBase::TestEventListenerBase;

    void sectionStarting( Catch::SectionInfo const &sectionInfo ) override {
        TestEventListenerBase::sectionStarting( sectionInfo );
        // Initialize the cata RNG with the Catch seed for reproducible tests
        rng_set_engine_seed( m_config->rngSeed() );
    }

    bool assertionEnded( Catch::AssertionStats const &assertionStats ) override {
#ifdef BACKTRACE
        Catch::AssertionResult const &result = assertionStats.assertionResult;

        if( result.getResultType() == Catch::ResultWas::FatalErrorCondition ) {
            // We are in a signal handler for a fatal error condition, so print a
            // backtrace
            stream << "Stack trace at fatal error:\n";
            debug_write_backtrace( stream );
        }
#endif

        return TestEventListenerBase::assertionEnded( assertionStats );
    }
};

CATCH_REGISTER_LISTENER( CataListener )

int main( int argc, const char *argv[] )
{
    Catch::Session session;

    std::vector<const char *> arg_vec( argv, argv + argc );

    std::vector<mod_id> mods = extract_mod_selection( arg_vec );
    mod_id def_core_mod_id = mod_management::get_default_core_content_pack();
    if( std::find( mods.begin(), mods.end(), def_core_mod_id ) == mods.end() ) {
        mods.insert( mods.begin(), def_core_mod_id ); // @todo move unit test items to core
    }

    option_overrides_t option_overrides_for_test_suite = extract_option_overrides( arg_vec );

    const bool dont_save = check_remove_flags( arg_vec, { "-D", "--drop-world" } );

    std::string user_dir = extract_user_dir( arg_vec );

    // Note: this must not be invoked before all DDA-specific flags are stripped from arg_vec!
    int result = session.applyCommandLine( arg_vec.size(), &arg_vec[0] );
    if( result != 0 || session.configData().showHelp ) {
        cata_printf( "CataclysmDDA specific options:\n" );
        cata_printf( "  --mods=<mod1,mod2,…>         Loads the list of mods before executing tests.\n" );
        cata_printf( "  --user-dir=<dir>             Set user dir (where test world will be created).\n" );
        cata_printf( "  -D, --drop-world             Don't save the world on test failure.\n" );
        cata_printf( "  --option_overrides=n:v[,…]   Name-value pairs of game options for tests.\n" );
        cata_printf( "                               (overrides config/options.json values)\n" );
        return result;
    }

    test_mode = true;

    setupDebug( DebugOutput::std_err );

    // Set the seed for mapgen (the seed will also be reset before each test)
    const unsigned int seed = session.config().rngSeed();
    if( seed ) {
        rng_set_engine_seed( seed );

        // If the run is terminated due to a crash during initialization, we won't
        // see the seed unless it's printed out in advance, so do that here.
        printf( "Randomness seeded to: %u\n", seed );
    }
    DebugLog( DL::Info, DC::Main ) << "Randomness seeded to: " << seed;

    try {
        // TODO: Only init game if we're running tests that need it.
        init_global_game_state( mods, option_overrides_for_test_suite, user_dir );
    } catch( const std::exception &err ) {
        cata_print_stderr( string_format( "Terminated: %s\n", err.what() ) );
        cata_print_stderr( "Make sure that you're in the correct working directory and your data isn't corrupted.\n" );
        return EXIT_FAILURE;
    }

    bool error_during_initialization = debug_has_error_been_observed();

    const auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t( start );
    // Leading newline in case there were debug messages during
    // initialization.
    cata_printf( "\nStarting the actual test at %s", std::ctime( &start_time ) );
    result = session.run();
    const auto end = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t( end );

    auto world_name = world_generator->active_world->world_name;
    if( result == 0 || dont_save ) {
        world_generator->delete_world( world_name, true );
    } else {
        cata_printf( "Test world \"%s\" left for inspection.\n", world_name.c_str() );
    }

    std::chrono::duration<double> elapsed_seconds = end - start;
    cata_printf( "Ended test at %sThe test took %.3f seconds\n", std::ctime( &end_time ),
                 elapsed_seconds.count() );

    if( seed ) {
        // Also print the seed at the end so it can be easily found
        DebugLog( DL::Info, DC::Main ) << "Randomness seeded to: " << seed;
    }

    if( error_during_initialization ) {
        cata_printf( "\nTreating result as failure due to error logged during initialization.\n" );
        cata_printf( "Randomness seeded to: %u\n", seed );
        return 1;
    }

    if( debug_has_error_been_observed() ) {
        cata_printf( "\nTreating result as failure due to error logged during tests.\n" );
        cata_printf( "Randomness seeded to: %u\n", seed );
        return 1;
    }

    cata_printf( "\n" );

    return result;
}
