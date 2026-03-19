# vim: set ss=4 ft= sw=4 et sts=4 ts=4:

use lib '.';
use t::TestLJ;

plan tests => 3 * blocks();

run_tests();

__DATA__

=== TEST 1: uncaught file error exits non-zero
--- lua
jit.off()
error("boom")
--- out
--- err
boom
--- exit: 1
