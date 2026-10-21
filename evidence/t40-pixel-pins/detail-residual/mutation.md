## The mutation (measured, 2026-10-21 17:44:19-17:48:54)

`_shared/t45/detail-prefix.sh` ran under the shared host lock on tree `6888e6d`
(exe sha256 `096f2c1f4c6506f6c322077af2d3be1789ad446ecc648168185ee528d2942784`):

1. captured the current tree's A and F at tick 2000: A_ref `6bb4d9a7...`,
   F_ref `2ffa7d2b...` -- both reproduce T40's pins;
2. reverted ONE line -- T29's near-plane expression -- to the box-unit constant
   (`? 0.05f : 0.05f;`): source sha `85b00d68...` -> `fe39ae5d...`, rebuilt
   (exe `64ff69e7...`), and the product's own trace reads
   `gpu: world clip near=0.05000000 far=100.000000 nearMapUnits=610.22 farMapUnits=1220444 horizontalScale=0.00008194 state=published`
   -- the pre-fix 611-map-unit clip;
3. captured A_mut and F_mut at tick 2000:
   * A_mut = `f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608` ==
     the kept pre-fix A, **0 of 2,582,352 bytes differ** (max_delta 0);
   * F_mut = `21cb8aa28df87b6c869e12a12be240ab79c0d5d5370bf97b073652890280647f` ==
     the kept pre-fix F, **0 bytes differ** -- including all 2,812 residual pixels;
4. restored the line (`cmp` clean, source sha `85b00d68...` before and after),
   rebuilt, and A_res `6bb4d9a7...` / F_res `2ffa7d2b...` reproduce the current
   pins exactly.

So the residual is caused by T29's near-clip change alone: one expression, two
frames on each side, and each side's frame is byte-identical to the frame the fix
produced. F_mut vs the current F differs on 1,560,453 bytes (60.43%, max_delta
234), the same order as A_mut vs A_ref (1,551,760 bytes): the detail leg is not a
special case, it rides on the whole frame.

    readings.txt   the full mutation readings block (frames, traces, diffs)
    detail-prefix.sh  the script that ran the mutation (documentation copy of
                   _shared/t45/detail-prefix.sh, which sources _shared/t45/lib.sh
                   and runs one capture at a time under the host lock)
