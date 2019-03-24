package ru.cobra.zap.afrd;

/**
 * Here's where shit hits the fun.
 * Java has a remarkably wretched functionality when it comes to low-level
 * data types such as byte or C strings. This crippled class is meant to fill
 * some gaps in this horrible java world.
 */
class javafun
{
    /**
     * Compare two C strings until first 0 is encountered in either of the strings.
     * Returns 0 if strings are equal, or a positive value if first different
     * value in str1 is larger than same value in str2, or a negative value.
     *
     * @param arr1 The byte array containing first string
     * @param ofs1 the offset of first string within first byte array
     * @param arr2 The byte array containing second string
     * @param ofs2 the offset of second string within second byte array
     * @return 0 if strings are equal, or a positive or negative value.
     */
    static int strcmp (byte [] arr1, int ofs1, byte [] arr2, int ofs2)
    {
        while (ofs1 < arr1.length && ofs2 < arr2.length)
        {
            int b1 = arr1 [ofs1++] & 0xff;
            int b2 = arr2 [ofs2++] & 0xff;
            int diff = b1 - b2;
            if (b1 == 0 || b2 == 0)
                return diff;
            if (diff != 0)
                return diff;
        }
        return 0;
    }
}
