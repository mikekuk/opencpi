-- This file is protected by Copyright. Please refer to the COPYRIGHT file
-- distributed with this source distribution.
--
-- This file is part of OpenCPI <http://www.opencpi.org>
--
-- OpenCPI is free software: you can redistribute it and/or modify it under the
-- terms of the GNU Lesser General Public License as published by the Free
-- Software Foundation, either version 3 of the License, or (at your option) any
-- later version.
--
-- OpenCPI is distributed in the hope that it will be useful, but WITHOUT ANY
-- WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
-- A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
-- details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program. If not, see <http://www.gnu.org/licenses/>.

-------------------------------------------------------------------------------
-- Cascaded Integrator-Comb (CIC) Decimator
-------------------------------------------------------------------------------
--
-- Implements a Cascaded Integrator-Comb Decimator
-- References for theory of operation:
--
--     Whitepaper by Matthew P. Donadio
--     http://home.mit.bme.hu/~kollar/papers/cic.pdf
--
--     Understanding Digital Signal Processing, 2nd Ed, Ricard G. Lyons
--     Section 10.5 - Cascaded Integrator-Comb Filters
--
-- The generic parameters for this design are as follows:
--
--    N = Number of CIC stages
--    M = Differential delay for comb sections
--    R = Decimation Factor
--    DIN_WIDTH = Input data width
--    DOUT_WIDTH = Output data width
--    ACC_WIDTH = Internal accumulator width
--
-- For proper operation, the accumulator width needs to be set according to
-- the following formula:
--
--    ACC_WIDTH >= CEIL(N*log2(R*M))+DIN_WIDTH
--
-- This will ensure that the accumulators can accommodate the maximum
-- difference between any two adjacent samples. Because of this and the use
-- of twos complement math, the comb sections will always compute the proper
-- difference from consecutive integrator outputs.
--
-- The DC gain of the CIC will be unity when R is a power of 2. A general
-- formula for the DC gain given N, M and R is:
--
--    CIC Gain = (R*M)^N / 2^CEIL(N*log2(R*M))
--
-------------------------------------------------------------------------------
-- Revision Log:
-------------------------------------------------------------------------------
-- 10/10/12:
-- File Created
-------------------------------------------------------------------------------
-- 08/14/15:
-- Changed all resets from async to sync
-- Change decimator to accommodate DIN_VLD < 100%
-------------------------------------------------------------------------------
-- 08/20/15:
-- Registered comb(0) and comb_vld for improved timing
-- Added notes
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity cic_dec_gen is
  generic(
    N          : positive := 3;
    M          : positive := 1;
    R          : positive := 4;
    DIN_WIDTH  : positive := 16;
    ACC_WIDTH  : positive := 22;
    DOUT_WIDTH : positive := 16);
  port(
    CLK      : in  std_logic;
    RST      : in  std_logic;
    DIN_VLD  : in  std_logic;
    DIN      : in  std_logic_vector(DIN_WIDTH-1 downto 0);
    DOUT_VLD : out std_logic;
    DOUT     : out std_logic_vector(DOUT_WIDTH-1 downto 0));
end cic_dec_gen;

architecture rtl of cic_dec_gen is

  constant DEC_CNT_WIDTH : integer := integer(ceil(log(real(R))/log(2.0)));

  type integ_t is array (0 to N) of std_logic_vector(ACC_WIDTH-1 downto 0);
  type comb_t is array (0 to N) of std_logic_vector(ACC_WIDTH-1 downto 0);
  type comb_dly_t is array (0 to N, 0 to M) of std_logic_vector(ACC_WIDTH-1 downto 0);

  signal integ        : integ_t;
  signal dec_cnt      : std_logic_vector(DEC_CNT_WIDTH-1 downto 0);
  signal comb_vld_pre : std_logic;
  signal comb_vld     : std_logic;
  signal comb         : comb_t;
  signal comb_dly     : comb_dly_t;

begin

  ----------------------------------------------------------------------------
  -- Integrator Stages
  ----------------------------------------------------------------------------

  integ(0)(ACC_WIDTH-1 downto DIN_WIDTH) <= (others => DIN(DIN_WIDTH-1));
  integ(0)(DIN_WIDTH-1 downto 0)         <= DIN;

  gen_integ : for i in 1 to N generate

    integ_i : process(CLK)
    begin
      if rising_edge(CLK) then
        if (RST = '1') then
          integ(i) <= (others => '0');
        elsif (DIN_VLD = '1') then
          integ(i) <= std_logic_vector(signed(integ(i)) + signed(integ(i-1)));
        end if;
      end if;
    end process;

  end generate;

  ----------------------------------------------------------------------------
  -- Decimator
  ----------------------------------------------------------------------------

  proc_dec_cnt : process(CLK)
  begin
    if rising_edge(CLK) then
      if (RST = '1') then
        dec_cnt <= (others => '0');
      elsif (DIN_VLD = '1') then
        if (unsigned(dec_cnt) = R-1) then
          dec_cnt <= (others => '0');
        else
          dec_cnt <= std_logic_vector(unsigned(dec_cnt)+1);
        end if;
      end if;
    end if;
  end process;

  comb_vld_pre <= '1' when (unsigned(dec_cnt) = R-1) and (DIN_VLD = '1') else '0';

  ----------------------------------------------------------------------------
  -- Comb Stages
  ----------------------------------------------------------------------------

  process(CLK)
  begin
    if rising_edge(CLK) then
      comb_vld <= comb_vld_pre;
      comb(0)  <= integ(N);
    end if;
  end process;

  gen_comb : for i in 1 to N generate

    comb_i : process(CLK)
    begin
      if rising_edge(CLK) then
        if (RST = '1') then
          comb(i) <= (others => '0');
        elsif (comb_vld = '1') then
          comb(i) <= std_logic_vector(signed(comb(i-1)) - signed(comb_dly(i, M)));
        end if;
      end if;
    end process;

    comb_dly(i, 0) <= comb(i-1);

    gen_comb_dly : for j in 1 to M generate
      comb_dly_j : process(CLK)
      begin
        if rising_edge(CLK) then
          if (RST = '1') then
            comb_dly(i, j) <= (others => '0');
          elsif (comb_vld = '1') then
            comb_dly(i, j) <= comb_dly(i, j-1);
          end if;
        end if;
      end process;
    end generate;

  end generate;

  ----------------------------------------------------------------------------
  -- Final Output
  ----------------------------------------------------------------------------

  proc_oReg : process(CLK)
  begin
    if rising_edge(CLK) then
      if (RST = '1') then
        DOUT     <= (others => '0');
        DOUT_VLD <= '0';
      elsif (comb_vld = '1') then
        DOUT     <= comb(N)(ACC_WIDTH-1 downto ACC_WIDTH-DOUT_WIDTH);
        DOUT_VLD <= '1';
      else
        DOUT_VLD <= '0';
      end if;
    end if;
  end process;

end rtl;
