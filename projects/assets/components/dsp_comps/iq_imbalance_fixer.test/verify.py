#!/usr/bin/env python2
# This file is protected by Copyright. Please refer to the COPYRIGHT file
# distributed with this source distribution.
#
# This file is part of OpenCPI <http://www.opencpi.org>
#
# OpenCPI is free software: you can redistribute it and/or modify it under the
# terms of the GNU Lesser General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# OpenCPI is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this program. If not, see <http://www.gnu.org/licenses/>.

"""
IQ Imbalance Fixer: Verify output data

Tested numpy version(s): 1.7.1

Verify args:
1. Number of complex signed 16-bit samples to validate
2. Output file used for validation
3. Input file used for comparison

To test the IQ Imbalance Fixer, a binary data file is generated containing complex
signed 16-bit samples with tones at 5Hz, 13Hz, and 27Hz. A phase offset of 10
degrees is applied to the Q channel, and then different gain amounts between the
I and Q rails, which result in a spectral image in the range of -Fs/2 to DC.

To validate the test, the output file is examined using FFT analysis to determine
that the spectral image has been removed and the tones are still present and of
sufficient power in the range DC to Fs/2.
"""
import os.path
import shutil
import struct
import sys
import opencpi.colors as color
import numpy as np
import math

class SampledData:
  def __init__(self, data, fs):
      """
      Parameters
      ----------------
      data
          Sampled data.
      fs
          Sampling frequency of sampled data in Hz.
      """
      self.data = data
      self.fs = fs

  def get_time_series(self, start_time):
      return start_time + (np.arange(len(self.data),dtype=float) / self.fs)

class DFTResult:
    """
    Represents the result of a one-dimensional Discrete Fourier Transform. For
    more info, see
    https://en.wikipedia.org/wiki/Discrete_Fourier_transform.
    """
    def __init__(self, sampled_data, n = None):
        """
        Parameters
        ----------------
        sampled_data
            Sampled data on which DFT will be calculated.
        n
            Length of the transformed axis of the output.
        """
        self.amplitudes = np.fft.fft(sampled_data.data, n)
        end = sampled_data.fs*(1-(1/n))
        self.freq_bins = np.arange(0., end, sampled_data.fs/n)

    def get_num_dft_points(self):
        return len(self.amplitudes)

    def get_unity_magnitude_normalization_factor(self):
        """
        Returns
        ----------------
            Returns amplitude relative to sampled_data unity, e.g. if
            sampled_data is a complex sinusoid of magnitude 1, the maximum value
            within the DFT result should be close to 1 after the result is
            multiplied by the value returned.
        """
        return 1. / self.get_num_dft_points()

class DFTCalculator():
    """
    Calculates a one-dimensional Discrete Fourier Transform. For more info, see
    https://en.wikipedia.org/wiki/Discrete_Fourier_transform.
    """
    def __init__(self, sampled_data):
        self.sampled_data = sampled_data

    def calc(self, n = None):
        """
        Parameters
        ----------------
        n
            Length of the transformed axis of the output.
        """
        self.result = DFTResult(self.sampled_data, n)
        return self.result

    def get_idx_of_nearest_freq_in_result(self, f):
        """ This has been verified both for positive and negative frequencies.
        Parameters
        ----------------
        f
            Frequency value in Hz.
        """
        fs = self.sampled_data.fs
        #return int(round(float(f)/float(fs)*float(len(self.result.amplitudes))))
        return int(round(float(f)/float(fs)*float(len(self.result.amplitudes))))

    def get_freq_for_idx_in_result(self, idx):
        """
        Parameters
        ----------------
        idx
            Zero-based index of DFT result, where 0 corresponds to 0 Hz, and the
            last index of the DFT result plus one corresponds to fs Hz.
        """
        fs = self.sampled_data.fs
        return float(idx)/float(self.result.get_num_dft_points())*float(fs)

    def get_nearest_freq_in_result(self, f):
        """
        Parameters
        ----------------
        f
            Frequency value in Hz.
        """
        idx = self.get_idx_of_nearest_freq_in_result(f)
        return self.get_freq_for_idx_in_result(idx)

    def get_magnitude_of_nearest_freq_in_result(self, f,
            unit = "dB_relative_to_unity"):
        """
        Parameters
        ----------------
        f
            Frequency value in Hz.
        Returns
        ----------------
            When unit is "dB_relative_to_unity", returns amplitude in dB
            relative to sampled_data unity, e.g. if sampled_data is a complex
            sinusoid of amplitude 1, the returned value should be
            close to 0 dB.
        """
        eps = pow(10, -10) # error factor to avoid divide by zero in log10
        idx = self.get_idx_of_nearest_freq_in_result(f)
        if unit == None:
            factor = 1
        elif unit == "dB_relative_to_unity":
            factor = self.result.get_unity_magnitude_normalization_factor()
        else:
            msg = "unit was was unsupported value of " + unit
            msg += ", supported values are None and dB_relative_to_unity"
            raise Exception(msg)
        abs_amp = abs(self.result.amplitudes[idx] * factor)
        ret = 20*np.log10(abs_amp + eps)
        return ret

    def get_max_magnitude_of_positive_freqs(self,
        unit = "dB_relative_to_unity"):
        eps = pow(10, -10) # error factor to avoid divide by zero in log10
        if unit == None:
            factor = 1
        elif unit == "dB_relative_to_unity":
            factor = self.result.get_unity_magnitude_normalization_factor()
        else:
            msg = "unit was was unsupported value of " + unit
            msg += ", supported values are None and dB_relative_to_unity"
            raise Exception(msg)
        nn = self.result.get_num_dft_points()
        result_pos_freqs = self.result.amplitudes[0:(nn/2)-1]
        tmp = 20*np.log10(abs(result_pos_freqs) + eps)
        ret = tmp[np.argmax(tmp)]
        return ret

    def get_max_magnitude_of_negative_freqs(self,
        unit = "dB_relative_to_unity"):
        eps = pow(10, -10) # error factor to avoid divide by zero in log10
        if unit == None:
            factor = 1
        elif unit == "dB_relative_to_unity":
            factor = self.result.get_unity_magnitude_normalization_factor()
        else:
            msg = "unit was was unsupported value of " + unit
            msg += ", supported values are None and dB_relative_to_unity"
            raise Exception(msg)
        nn = self.result.get_num_dft_points()
        result_neg_freqs = self.result.amplitudes[nn/2:nn-1]
        tmp = 20*np.log10(abs(result_neg_freqs) + eps)
        ret = tmp[np.argmax(tmp)]
        return ret

def calc_nearest_freq_and_mag(desired_freq, calc, pre_msg):
    nearest_freq = calc.get_nearest_freq_in_result(desired_freq)
    mag = calc.get_magnitude_of_nearest_freq_in_result(desired_freq,
        unit="dB_relative_to_unity")
    msg = pre_msg + 'Tone at   ' + str(nearest_freq) + ' Hz has magnitude of '
    msg += str(mag) + '\tdB relative to unity'
    print(msg)
    return [nearest_freq, mag]

def test_expected_max_gain_diff(freq, in_calc, out_calc, max_allowed_gain_diff_dB):
    """
    Parameters
    ----------------
    freq
        Frequency in Hz for tone to be tested
    in_calc
        DFTCalculator object for data input to iq_imbalance_fixer
    out_calc
        DFTCalculator object for data output from iq_imbalance_fixer
    max_allowed_gain_diff_dB
        Maximum pre-to-post tone gain difference for which a test will succeed.
    """
    [in_freq, in_mag]   = calc_nearest_freq_and_mag(freq, in_calc,  "Input        ")
    [out_freq, out_mag] = calc_nearest_freq_and_mag(freq, out_calc, "Output       ")

    gain = out_mag - in_mag
    if abs(gain) > max_allowed_gain_diff_dB:
        msg = 'FAILED, Tone in->out gain = ' + str(gain)
        msg += " dB, which was greater than the maximum "
        msg += "allowed difference of " + str(max_allowed_gain_diff_dB) + " dB"
        print(color.RED + color.BOLD + msg + color.END)
        sys.exit(1)

def test_expected_image_tone_suppression(freq, in_calc, out_calc, min_allowed_suppression_dB):
    """
    Parameters
    ----------------
    freq
        Frequency in Hz for tone to be tested
    in_calc
        DFTCalculator object for data input to iq_imbalance_fixer
    out_calc
        DFTCalculator object for data output from iq_imbalance_fixer
    min_allowed_suppression_dB
        Minimum required suppression for an image tone for which a test will succeed
    """
    [in_freq, in_mag]   = calc_nearest_freq_and_mag(freq, in_calc,  "Input  image ")
    [out_freq, out_mag] = calc_nearest_freq_and_mag(freq, out_calc, "Output image ")

    if (in_mag - out_mag) < min_allowed_suppression_dB:
        msg = 'FAILED, Output image Tone level was suppressed by '
        msg += str(in_mag - out_mag) + " dB (in comparison to the input "
        msg += "tone), which is less than the minimum allowed value of "
        msg += str(min_allowed_suppression_dB) + " dB"
        print(color.RED + color.BOLD + msg + color.END)
        sys.exit(1)

def test_expected_min_pos_neg_freq_amp_diff(out_calc, num_samples, min_amp_diff_dB):
    max_neg_freq = out_calc.get_max_magnitude_of_negative_freqs(unit="dB_relative_to_unity")
    max_pos_freq = out_calc.get_max_magnitude_of_positive_freqs(unit="dB_relative_to_unity")
    print 'Output maximum magnitude from [-Fs/2 to 0) = ', max_neg_freq, ' \tdB relative to unity'
    print 'Output maximum Frequency from [0 to +Fs/2) = ', max_pos_freq, ' \tdB relative to unity'

    #compare max tone in range DC to +Fs/2 to max value of noise floor in range -Fs/2 to DC
    if max_pos_freq - max_neg_freq < min_amp_diff_dB:
        print color.RED + color.BOLD + 'FAILED, Noise floor from -Fs/2 to 0 too high' + color.END
        sys.exit(1)

print "\n","*"*80
print "*** Python: IQ Imbalance Fixer ***"

print "*** Validation of IQ Imbalance Fixer output (binary data file) ***"
if len(sys.argv) != 4:
    print("Invalid arguments:  usage is: verify.py <num-samples> <output-file> <input-file>")
    sys.exit(1)

num_samples = int(sys.argv[1])
ofilename = sys.argv[2]
ifilename = sys.argv[3]

dt_iq_pair = np.dtype((np.uint32, {'real_idx':(np.int16,0), 'imag_idx':(np.int16,2)}))

#Read input and output data files as complex int16
ifile = open(ifilename, 'rb')
din = np.fromfile(ifile, dtype=dt_iq_pair, count=-1)
ifile.close()
ofile = open(ofilename, 'rb')
dout = np.fromfile(ofile, dtype=dt_iq_pair, count=-1)
ofile.close()

enable = os.environ.get("OCPI_TEST_enable")

if(enable=="true"): # => NORMAL MODE
    #Throw away the first half of the output file to remove the start-up transients
    dout_normal = dout[num_samples/2:num_samples]

    #Ensure dout is not all zeros
    if all(dout_normal == 0):
        print color.RED + color.BOLD + 'FAILED, values are all zero' + color.END
        sys.exit(1)

    #Ensure that dout is the expected amount of data
    if len(dout_normal) != num_samples/2:
        print color.RED + color.BOLD + 'FAILED, output file length is unexpected' + color.END
        print color.RED + color.BOLD + 'Length dout = ', len(dout_normal)/2, 'while expected length is = ' + color.END, num_samples
        sys.exit(1)

    #share values used during generation of the input file
    #convert to complex data type to perform fft and power measurements
    freqT1_Hz = 5.
    freqT2_Hz = 13.
    freqT3_Hz = 27.
    Fs = 100.

    complex_idata = np.array(np.zeros(num_samples), dtype=np.complex)
    complex_odata = np.array(np.zeros(num_samples/2), dtype=np.complex)
    for i in xrange(0,num_samples):
        complex_idata[i] = complex(din['real_idx'][i], din['imag_idx'][i])
    for i in xrange(0,num_samples/2):
        complex_odata[i] = complex(dout_normal['real_idx'][i], dout_normal['imag_idx'][i])

    in_calc = DFTCalculator(SampledData(complex_idata, Fs))
    in_calc.calc(n = num_samples)
    out_calc = DFTCalculator(SampledData(complex_odata, Fs))

    #out_calc.calc(n = num_samples/2) # this causes nearest freq
                                      # calculations to be mismatched between
                                      # in_calc and out_calc
    out_calc.calc(n = num_samples) # this allows nearest freq
                                   # calculations to be the same between
                                   # in_calc and out_calc


    # TODO / FIXME - replace this tests with ones that don't contain
    # emperically chosen min/max-allowed values
    test_expected_max_gain_diff(freqT1_Hz, in_calc, out_calc, max_allowed_gain_diff_dB = 6.9)
    test_expected_max_gain_diff(freqT2_Hz, in_calc, out_calc, max_allowed_gain_diff_dB = 6.4)
    test_expected_max_gain_diff(freqT3_Hz, in_calc, out_calc, max_allowed_gain_diff_dB = 6.6)
    test_expected_image_tone_suppression(-freqT1_Hz, in_calc, out_calc, min_allowed_suppression_dB = 67.9)
    test_expected_image_tone_suppression(-freqT2_Hz, in_calc, out_calc, min_allowed_suppression_dB = 67.6)
    test_expected_image_tone_suppression(-freqT3_Hz, in_calc, out_calc, min_allowed_suppression_dB = 73.3)
    test_expected_min_pos_neg_freq_amp_diff(out_calc, num_samples, min_amp_diff_dB = 64.4)

    print 'Data matched expected results.'
    print color.GREEN + color.BOLD + 'PASSED' + color.END
    print '*** End validation ***\n'
else: # => BYPASS MODE
    #There is a 4 sample latency in processing, so the first 4 samples of the output are 0. Correcting for that here
    din_bypass = din[0:num_samples-4]
    dout_bypass = dout[4:num_samples]
    #Test that odata is the expected amount
    if (din_bypass != dout_bypass).all():
        print color.RED + color.BOLD + "FAILED: Input and output file do not match" + color.END
        sys.exit(1)
    else:
        print color.GREEN + color.BOLD + "PASSED: Input and output file match" + color.END
