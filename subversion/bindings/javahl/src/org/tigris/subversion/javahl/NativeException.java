/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */

package org.tigris.subversion.javahl;

/**
 * This exception is thrown whenever something goes wrong in the
 * Subversion JavaHL binding's JNI code.
 */
class NativeException extends SubversionException
{
    // Update the serialVersionUID when there is an incompatible change made to
    // this class.  See the Java documentation (following link or its counter-
    // part in your specific Java release) for when a change is incompatible.
    // https://docs.oracle.com/en/java/javase/11/docs/specs/serialization/version.html#type-changes-affecting-serialization
    private static final long serialVersionUID = 1L;

    /**
     * Any associated error source (e.g. file name and line number)
     * for a wrapped <code>svn_error_t</code>.
     */
    private String source;

    /**
     * Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    private int aprError;

    /**
     * This constructor is only used by the native library.
     *
     * @param message A description of the problem.
     * @param source The error's source.
     * @param aprError Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    NativeException(String message, String source, int aprError)
    {
        super(message);
        this.source = source;
        this.aprError = aprError;
    }

    /**
     * @return The error source (e.g. line number).
     */
    public String getSource()
    {
        return source;
    }

    /**
     * @return Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    public int getAprError()
    {
        return aprError;
    }

    /**
     * @return The description, with {@link #source} and {@link
     * #aprError} appended (if any).
     */
    public String getMessage()
    {
        StringBuffer msg = new StringBuffer(super.getMessage());
        // ### This might be better off in JNIUtil::handleSVNError().
        String src = getSource();
        if (src != null)
        {
            msg.append("svn: ");
            msg.append(src);
            int aprErr = getAprError();
            if (aprErr != -1)
            {
                msg.append(": (apr_err=").append(aprErr).append(')');
            }
        }
        return msg.toString();
    }
}
